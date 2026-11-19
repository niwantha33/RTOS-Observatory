// SPDX-License-Identifier: Apache-2.0
#include "observatory/framed_tcp.hpp"
#include <utility>
namespace observatory {
std::shared_ptr<FramedTcpCapture> FramedTcpCapture::create(
    asio::io_context& io,FramedTcpOptions options,Completion completion) {
    if(!completion || !options.port || options.max_bytes<OBS_WIRE_MAX_FRAME ||
       !options.max_frames || options.idle_timeout.count()<=0 ||
       options.total_timeout.count()<=0)
        throw Error("invalid W17 framed TCP capture options");
    asio::error_code ec;
    (void)asio::ip::make_address(options.address,ec);
    if(ec)throw Error("W17 TCP capture requires numeric IP (no DNS)");
    return std::shared_ptr<FramedTcpCapture>(
        new FramedTcpCapture(io,std::move(options),std::move(completion)));
}
FramedTcpCapture::FramedTcpCapture(asio::io_context& io,
                                   FramedTcpOptions options,Completion completion)
    : strand_(asio::make_strand(io)),socket_(strand_),idle_(strand_),
      deadline_(strand_),options_(std::move(options)),
      complete_(std::move(completion)) {
    obs_wire_decoder_init(&decoder_);
}
void FramedTcpCapture::start() {
    asio::post(strand_,[self=shared_from_this()] {self->begin();});
}
void FramedTcpCapture::stop() {
    asio::post(strand_,[self=shared_from_this()] {
        self->finish("framed TCP capture cancelled");
    });
}
void FramedTcpCapture::begin() {
    if(started_ || done_)return;
    started_=true;
    const auto self=shared_from_this();
    deadline_.expires_after(options_.total_timeout);
    deadline_.async_wait([self](const asio::error_code& ec) {
        if(!ec)self->finish("framed TCP capture duration exceeded");
    });
    arm_idle();
    socket_.async_connect(
        {asio::ip::make_address(options_.address),options_.port},
        [self](const asio::error_code& ec) {
            if(self->done_)return;
            if(ec){self->finish("framed TCP connect failed: "+ec.message());return;}
            self->read();
        });
}
void FramedTcpCapture::arm_idle() {
    const auto generation=++idle_generation_;
    idle_.expires_after(options_.idle_timeout);
    idle_.async_wait([self=shared_from_this(),generation](const asio::error_code& ec){
        if(!ec && generation==self->idle_generation_)
            self->finish("framed TCP capture idle timeout");
    });
}
void FramedTcpCapture::read() {
    socket_.async_read_some(asio::buffer(buffer_),
        [self=shared_from_this()](const asio::error_code& ec,std::size_t n) {
            if(self->done_)return;
            if(n>self->options_.max_bytes-self->result_.bytes) {
                self->finish("framed TCP byte budget exceeded");return;
            }
            self->result_.bytes+=n;
            for(std::size_t i=0;i<n;++i) {
                obs_wire_frame_t frame{};
                const auto kind=obs_wire_decoder_push(&self->decoder_,
                                                     self->buffer_[i],&frame);
                if(kind!=OBS_WIRE_FRAME_READY)continue;
                if(self->result_.frames>=self->options_.max_frames) {
                    self->finish("framed TCP frame budget exceeded");return;
                }
                ++self->result_.frames;
                if(frame.kind==OBS_WIRE_EVENT)++self->result_.events;
                else if(frame.kind==OBS_WIRE_STATS)++self->result_.stats;
            }
            if(ec==asio::error::eof) {
                if(self->decoder_.used!=0u) {
                    self->finish("framed TCP truncated record at EOF");return;
                }
                if(self->result_.frames==0u) {
                    self->finish("framed TCP received no valid frames");return;
                }
                self->finish({},true);return;
            }
            if(ec){self->finish("framed TCP read failed: "+ec.message());return;}
            self->arm_idle();
            self->read();
        });
}
void FramedTcpCapture::finish(std::string error,bool eof) {
    if(done_)return;
    done_=true;
    asio::error_code ignored;
    socket_.cancel(ignored);
    socket_.close(ignored);
    idle_.cancel();
    deadline_.cancel();
    result_.crc_errors=decoder_.crc_errors;
    result_.invalid_frames=decoder_.invalid_frames;
    result_.replayed_frames=decoder_.replayed_frames;
    result_.missing_frames=decoder_.missing_frames;
    result_.missing_attempts=decoder_.missing_attempts;
    result_.epoch_changes=decoder_.epoch_changes;
    result_.loss_saturated=decoder_.loss_saturated;
    result_.clean_eof=eof;
    result_.error=std::move(error);
    auto callback=std::move(complete_);
    callback(std::move(result_));
}
} // namespace observatory
