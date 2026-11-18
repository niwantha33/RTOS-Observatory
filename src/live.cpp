// SPDX-License-Identifier: Apache-2.0
#include "observatory/live.hpp"
namespace observatory {
std::shared_ptr<TcpCapture> TcpCapture::create(asio::io_context& io, TcpCaptureOptions options, Completion cb) {
    if (!cb || !options.port || options.idle_timeout.count()<=0 || options.total_timeout.count()<=0 ||
        options.limits.max_bytes==0 || options.limits.max_events==0 || options.limits.max_line_bytes==0)
        throw Error("invalid TCP capture options");
    asio::error_code ec;
    asio::ip::make_address(options.address, ec);
    if (ec) throw Error("TCP capture requires a numeric IP address");
    return std::shared_ptr<TcpCapture>(new TcpCapture(io,std::move(options),std::move(cb)));
}
TcpCapture::TcpCapture(asio::io_context& io, TcpCaptureOptions options, Completion cb)
    : strand_(asio::make_strand(io)), socket_(strand_), idle_(strand_), deadline_(strand_),
      options_(std::move(options)), completion_(std::move(cb)), decoder_(options_.limits) {}
void TcpCapture::start() {
    asio::post(strand_,[self=shared_from_this()]{self->begin();});
}
void TcpCapture::stop() {
    asio::post(strand_,[self=shared_from_this()]{self->finish("capture cancelled");});
}
void TcpCapture::begin() {
    if (started_ || done_) return;
    started_=true;
    auto self=shared_from_this();
    deadline_.expires_after(options_.total_timeout);
    deadline_.async_wait([self](const asio::error_code& ec){if(!ec) self->finish("capture duration exceeded");});
    arm_idle();
    socket_.async_connect({asio::ip::make_address(options_.address),options_.port},
        [self](const asio::error_code& ec) {
            if(self->done_) return;
            if(ec) {self->finish("connect failed: "+ec.message());return;}
            self->read();
        });
}
void TcpCapture::arm_idle() {
    const auto generation=++idle_generation_;
    idle_.expires_after(options_.idle_timeout);
    idle_.async_wait([self=shared_from_this(),generation](const asio::error_code& ec){
        if(!ec && generation==self->idle_generation_) self->finish("capture idle timeout");
    });
}
void TcpCapture::read() {
    socket_.async_read_some(asio::buffer(buffer_),[self=shared_from_this()](const asio::error_code& ec,std::size_t n) {
        if(self->done_) return;
        if(n > self->options_.limits.max_bytes-self->raw_.size()) {self->finish("capture byte limit exceeded");return;}
        self->raw_.append(self->buffer_.data(),n);
        try {
            self->decoder_.feed(std::string_view(self->buffer_.data(),n));
            if(self->options_.progress && n) self->options_.progress(self->decoder_.progress());
            if(ec==asio::error::eof) {
                auto summary=self->decoder_.progress();
                self->decoder_.finish(true);
                summary.finalized=true;
                if(self->options_.progress) self->options_.progress(summary);
                self->finish({},true);return;
            }
        } catch(const std::exception& error) {
            self->finish(std::string("stream validation/progress failed: ")+error.what());return;
        }
        if(ec) {self->finish("read failed: "+ec.message());return;}
        self->arm_idle();self->read();
    });
}
void TcpCapture::finish(std::string error,bool eof) {
    if(done_) return;
    done_=true;
    asio::error_code ignored;
    socket_.cancel(ignored);socket_.close(ignored);
    idle_.cancel();deadline_.cancel();
    auto cb=std::move(completion_);
    cb({std::move(raw_),std::move(error),eof});
}
} // namespace observatory
