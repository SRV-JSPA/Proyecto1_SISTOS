#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

void handle_session(tcp::socket socket) {
    try {
        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept();  

        std::cout << "Cliente conectado.\n";

        ws.write(net::buffer(std::string("Hola Mundo")));

        while (true) {
            beast::flat_buffer buffer;
            ws.read(buffer);  
            std::string received_msg = beast::buffers_to_string(buffer.data());

            std::cout << "Mensaje recibido: " << received_msg << std::endl;

            ws.write(net::buffer(std::string("Buenas tardes")));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error en sesión WebSocket: " << e.what() << std::endl;
    }
}

int main() {
    try {
        net::io_context ioc;
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 9000));

        std::cout << "Servidor WebSocket iniciado en el puerto 9000...\n";

        while (true) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);
            std::thread(&handle_session, std::move(socket)).detach();  
        }
    } catch (const std::exception& e) {
        std::cerr << "Error en el servidor: " << e.what() << std::endl;
    }

    return 0;
}
