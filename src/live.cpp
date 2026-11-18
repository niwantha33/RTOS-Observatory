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
      options_(std::move(options)), completion_(std::move(cb)) {}
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
        // Count physical data rows and bound partial lines before accumulating them.
        // One extra row is allowed for the universal-v1 header; decoder enforces exact event count.
        for(std::size_t i=0;i<n;++i) {
            const char c=self->buffer_[i];
            if(c=='\n') {
                if(self->row_has_data_ && !self->row_comment_) {
                    if(++self->rows_ > self->options_.limits.max_events && self->rows_-self->options_.limits.max_events>1) {
                        self->finish("capture row limit exceeded");return;
                    }
                }
                self->line_bytes_=0;self->row_has_data_=false;self->row_comment_=false;
            } else {
                if(++self->line_bytes_ > self->options_.limits.max_line_bytes) {self->finish("capture line limit exceeded");return;}
                if(!self->row_has_data_) self->row_comment_=(c=='#');
                self->row_has_data_=true;
            }
        }
        self->raw_.append(self->buffer_.data(),n);
        if(ec==asio::error::eof) {
            // A newline is the transport commit boundary: never silently accept a cut final row.
            if(self->line_bytes_!=0) self->finish("truncated stream: missing final newline");
            else self->finish({},true);
            return;
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
