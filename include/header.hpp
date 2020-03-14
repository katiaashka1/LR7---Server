// Copyright 2018 Your Name <your_email>

#ifndef INCLUDE_HEADER_HPP_
#define INCLUDE_HEADER_HPP_

#include <iostream>
#include <vector>
#include <mutex>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/asio.hpp>

#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
//#include <boost/log/support/date_time.hpp>
#include <boost/log/sources/severity_logger.hpp>
//#include <boost/log/utility/setup/console.hpp>
//#include <boost/log/sinks.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/log/expressions/keyword.hpp>

namespace keywords = boost::log::keywords;

namespace logging = boost::log;


using namespace std;
using namespace boost::asio;

io_service service;
boost::recursive_mutex mx;
class Client;
std::vector<std::shared_ptr<Client>> clients;

class Client{
private:
    ip::tcp::socket sock;
    enum { max_msg = 1024 };
    int already_read_;
    char buff_[max_msg];
    bool started_;
    std::string username_;
    bool clients_changed_;
    boost::posix_time::ptime last_ping;
public:
    Client() : sock(service),clients_changed_(false) {}

    ip::tcp::socket& sock_r(){
        return sock;
    }

    std::string username() const {
        return username_;
    }

    void answer_to_client(){
        try {
            read_request();
            process_request();
        }
        catch ( boost::system::system_error&)
        {
            stop();
        }
        if ( timed_out())
            stop();
    }

    void read_request() {
        if (sock.available())
            already_read_ += sock.read_some(buffer(buff_ + already_read_, max_msg - already_read_));
    }

    void process_request() {
        bool found_enter = std::find(buff_, buff_ + already_read_, '\n') < buff_ + already_read_;
        if (!found_enter)
            return;

        last_ping = boost::posix_time::microsec_clock::local_time();
        size_t pos = std::find(buff_, buff_ + already_read_, '\n') - buff_;
        std::string msg(buff_, pos);
        std::copy(buff_ + already_read_, buff_ + max_msg, buff_);
        already_read_ -= pos + 1;


        if (msg.find("login ") == 0) on_login(msg);
        else if (msg.find("ping") == 0) on_ping();
        else if (msg.find("ask_clients") == 0) on_clients();
        else std::cerr << "invalid msg " << msg << std::endl;
    }

    void on_login(const std::string & msg){
        std::istringstream in(msg);
        in >> username_ >> username_;
        //std::cout<< username_ <<std::endl;
        write("login ok\n");
        {
            boost::recursive_mutex::scoped_lock lk(mx);
            for(unsigned i=0;i<clients.size()-1;i++) {
                clients[i]->update_clients_changed();
            }
        }
    }

    void update_clients_changed(){
        clients_changed_ = true;
    }

    void on_ping(){
        //std::cout<<clients_changed_<<std::endl;
        write(clients_changed_ ? "ping client_list_changed\n" : "ping ok\n");
        clients_changed_ = false;
    }

    void on_clients(){
        std::string msg;
        {
            boost::recursive_mutex::scoped_lock lk(mx);
            for(auto b = clients.begin(), e = clients.end() ;b != e; ++b)
                msg += (*b)->username() + " ";
        }
        write("clients " + msg + "\n");
    }

    void write(const std::string & msg) {
        //cout<<msg<<endl;
        sock.write_some(buffer(msg));
    }

    void stop() {
        boost::system::error_code err;
        sock.close(err);
    }

    bool timed_out() const {
        boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
        long long ms = (now - last_ping).total_milliseconds();
        return ms > 5000 ;
    }
};

static void log_init() {
    logging::add_file_log (
            keywords::file_name = "info.log",
            keywords::format = "[%TimeStamp%] [%ThreadID%] [%Severity%] %Message%"
    );

    logging::add_console_log (
            std::cout,
            keywords::format = "[%TimeStamp%] [%ThreadID%] [%Severity%] %Message%"
    );

    logging::add_common_attributes();
}

void accept_thread(){
    ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(), 8001));
    log_init();
    while(true){
        std::shared_ptr<Client> cl = std::make_shared<Client>();
        std::cout<<"wait client"<<std::endl;
        acceptor.accept(cl->sock_r());
//        std::cout<<"client acepted"<<std::endl;
        BOOST_LOG_TRIVIAL(info) << "client acepted";
        boost::recursive_mutex::scoped_lock lock(mx);
        clients.push_back(cl);
    }
}

void handle_clients_thread(){
    while (true) {
        boost::this_thread::sleep(boost::posix_time::millisec(1));
        boost::recursive_mutex::scoped_lock lk(mx);
        for(auto b = clients.begin(); b != clients.end(); ++b)
            (*b)->answer_to_client();

        clients.erase(std::remove_if(clients.begin(), clients.end(),
                                     boost::bind(&Client::timed_out,_1)), clients.end());
    }
}



//int main(){
//    boost::thread_group threads;
//    threads.create_thread(accept_thread);
//    threads.create_thread(handle_clients_thread);
//    threads.join_all();
//    return 0;
//}

#endif // INCLUDE_HEADER_HPP_
