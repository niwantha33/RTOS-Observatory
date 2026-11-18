// SPDX-License-Identifier: Apache-2.0
#include "observatory/live.hpp"
#include <iostream>
#include <thread>
using namespace observatory;
void require(bool value) {if(!value) throw Error("live API test failed");}
int main() {
    try {
        asio::io_context io;
        TcpCaptureOptions options;options.port=1;
        unsigned completions=0;
        auto capture=TcpCapture::create(io,options,[&](CaptureResult r){
            ++completions;require(!r.clean_eof && r.error=="capture cancelled");
        });
        // Cancel-before-start must close exactly once, even when run on two workers.
        capture->stop();capture->start();capture->start();capture->stop();
        std::jthread worker([&]{io.run();});io.run();worker.join();
        require(completions==1);
        bool rejected=false;
        try {options.address="invalid-hostname";TcpCapture::create(io,options,[](auto){});}
        catch(const Error&) {rejected=true;}
        require(rejected);
        std::cout<<"Live lifecycle tests passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
