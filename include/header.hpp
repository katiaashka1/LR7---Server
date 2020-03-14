// Copyright 2018 Your Name <your_email>

#ifndef INCLUDE_HEADER_HPP_
#define INCLUDE_HEADER_HPP_

#include <iostream>
#include <vector>
#include <mutex>
#include <algorithm>
#include <string>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/asio.hpp>

#define MAX_MSG 1024


//using namespace boost::asio;

using std::cout;
using std::endl;

// Создаем экземпляр для общения с сервисом ввода/вывода ОС
boost::asio::io_service service;
boost::recursive_mutex mx;

class Server;

// Вектор клиентов
std::vector<std::shared_ptr<Server>> clients;

class Server {
private:
    boost::asio::ip::tcp::socket sock;
    int already_read_;
    char buff_[MAX_MSG];
    std::string username_;
    bool clients_changed_;
    boost::posix_time::ptime last_ping;

public:
    Server() : sock(service), clients_changed_(false) {}

    boost::asio::ip::tcp::socket &sock_r() {
        return sock;
    }

    std::string username() const {
        return username_;
    }

    void answer_to_client() {   // Ответ клиенту
        try {
            read_request();     // Чтение запроса
            process_request();  //Обработка запроса
        }
        // Обработка ошибки, которая может произойти в блоке try
        catch (boost::system::system_error &) {
            stop(); // Выключение сервера
        }

        // Провека на время
        // Если клиент не пингукется в теченнии 5 сек, то кикнуть его
        if (timed_out())
            stop();
    }

    void read_request() {   // Чтение запроса
        if (sock.available())
            already_read_ += sock.read_some(boost::asio::buffer
                    (buff_ + already_read_, MAX_MSG - already_read_));
    }

    void process_request() {    // Обработка полученого запроса
        bool found_enter = std::find(buff_, buff_ + already_read_, '\n')
                           < buff_ + already_read_;
        if (!found_enter)
            return;

        // Метка для засекания пинга
        last_ping = boost::posix_time::microsec_clock::local_time();

        // Парсим сообщение
        size_t pos = std::find(buff_, buff_ + already_read_, '\n') - buff_;
        std::string msg(buff_, pos);
        std::copy(buff_ + already_read_, buff_ + MAX_MSG, buff_);
        already_read_ -= pos + 1;

        if (msg.find("login ") == 0) on_login(msg);
        else if (msg.find("ping") == 0) on_ping();
        else if (msg.find("ask_clients") == 0) on_clients();
        else
            std::cerr << "invalid msg " << msg << std::endl;
    }

    void on_login(const std::string &msg) {    // Регистрация пользователя
        std::istringstream in(msg);
        in >> username_ >> username_;
        //std::cout<< username_ <<std::endl;
        write("login ok\n");
        {
            boost::recursive_mutex::scoped_lock lk(mx);
            // Каждый клиент выставляет флаг
            for (unsigned i = 0; i < clients.size() - 1; i++)
                clients[i]->update_clients_changed();
        }
    }

    // Обновление списка клиентов (выставление флага)
    void update_clients_changed() {
        clients_changed_ = true;
    }

    // Клиент может делать следующие запросы: получить список
    // всех подключенных клиентов и пинговаться,
    // где в ответе сервера будет либо ping_ok,
    // либо client_list_chaned
    // (в последнем случае клиент повторно запрашивает список
    // подключенных клиентов);
    void on_ping() {
        //std::cout<<clients_changed_<<std::endl;
        write(clients_changed_ ? "ping client_list_changed\n" : "ping ok\n");
        clients_changed_ = false;   // Сброс флага
    }

    void on_clients() { // Ответ клиенту
        std::string msg;
    {
            boost::recursive_mutex::scoped_lock lk(mx);
            for (auto b = clients.begin(), e = clients.end(); b != e; ++b)
                msg += (*b)->username() + " ";
        }
        write("clients " + msg + "\n");
    }

    void write(const std::string &msg) {   // Запись сообшениия
        //cout<<msg<<endl;
        sock.write_some(boost::asio::buffer(msg));
    }

    void stop() {   // Закрытие сокета
        boost::system::error_code err;
        sock.close(err);
    }

    bool timed_out() const {    // Подсчет времени для отключения
        boost::posix_time::ptime now =
                boost::posix_time::microsec_clock::local_time();
        int ms = (now - last_ping).total_milliseconds();
        return ms > 5000;
    }
};

// Поток для прослушивания новых клиентов
void accept_thread() {
    // Задаем порт для прослушивания и создаем акцептор (приемник)
    // — один объект, который принимает клиентские подключения
    boost::asio::ip::tcp::acceptor acceptor(service,
            boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 8001));

    while (true) {
        // Создаем  умный указатель cl (на сокет)
        std::shared_ptr<Server> cl = std::make_shared<Server>();
        std::cout << "wait client" << std::endl;
        acceptor.accept(cl->sock_r());  // Ждем подключение клиента
        std::cout << "client acepted" << std::endl;
        // Потокобезопасный доступ к вектору клииентов
        boost::recursive_mutex::scoped_lock lk(mx);
        clients.push_back(cl);  // Добавление нового клиента в вектор
    }
}

// Поток для прослушиваниия существующих клиентов
void handle_clients_thread() {
    while (true) {
        boost::this_thread::sleep(boost::posix_time::millisec(1));
        // Потокобезопасный доступ к вектору клииентов
        boost::recursive_mutex::scoped_lock lk(mx);

        for (auto b = clients.begin(); b != clients.end(); ++b)
            (*b)->answer_to_client();   // Отпраляем ответы КАЖДОМУ клиенту

        // Удаляем клиенты, у которых закончилось время
        clients.erase(std::remove_if(clients.begin(), clients.end(),
                    boost::bind(&Server::timed_out, _1)), clients.end());
    }
}


int main() {
    boost::thread_group threads;
    // Поток для прослушивания новых клиентов
    threads.create_thread(accept_thread);
    // Поток для обработки существующих клиентов
    threads.create_thread(handle_clients_thread);
    // Запуск потоков и ожидание завершения последнего
    threads.join_all();
    return 0;
}
#endif // INCLUDE_HEADER_HPP_
