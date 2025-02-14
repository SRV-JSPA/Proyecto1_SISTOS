#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class Usuario {
public:
    std::string nombre;
    std::string ip;
    std::string estatus;

    Usuario(std::string nombre, std::string ip)
        : nombre(std::move(nombre)), ip(std::move(ip)), estatus("conectado") {}

    void cambiar_estatus(const std::string& nuevo_estatus) {
        estatus = nuevo_estatus;
    }

    std::string obtener_estatus() const {
        return estatus;
    }

    std::string obtener_ip() const {
        return ip;
    }
    
};

std::unordered_map<std::string, std::shared_ptr<Usuario>> user_sessions;
std::mutex session_mutex;

void handle_session(tcp::socket socket) {
    try {
        std::string cliente_ip = socket.remote_endpoint().address().to_string();
        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept();

        beast::flat_buffer buffer;
        ws.read(buffer);

        std::vector<uint8_t> received_msg(buffer.size());
        boost::asio::buffer_copy(boost::asio::buffer(received_msg), buffer.data());

        if (received_msg.size() < 3 || received_msg[0] != 53) {
            ws.write(net::buffer(std::string("Error: Formato de registro inválido")));
            return;
        }

        uint8_t usuario_length = received_msg[1];
        std::string usuario(received_msg.begin() + 2, received_msg.begin() + 2 + usuario_length);

        {
            std::lock_guard<std::mutex> lock(session_mutex);
            if (user_sessions.find(usuario) != user_sessions.end()) {
                ws.write(net::buffer(std::string("Error: Usuario ya registrado")));
                return;
            }

            user_sessions[usuario] = std::make_shared<Usuario>(usuario, cliente_ip);
        }

        std::cout << "Nuevo usuario registrado: " << usuario << " | IP: " << cliente_ip << "\n";
        ws.write(net::buffer("Registro exitoso: " + usuario));

        while (true) {
            buffer.clear();
            ws.read(buffer);
            std::string msg = beast::buffers_to_string(buffer.data());

            std::cout << "Mensaje recibido de " << usuario << ": " << msg << std::endl;
            ws.write(net::buffer("Echo: " + msg));
        }

    } catch (const beast::system_error& se) {
        if (se.code() == websocket::error::closed) {
            std::cout << "Usuario desconectado." << std::endl;
        } else {
            std::cerr << "Error en sesión WebSocket: " << se.what() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error en sesión WebSocket: " << e.what() << std::endl;
    }
}

int main() {
    try {
        net::io_context ioc;
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 9000));

        std::cout << "Servidor WebSocket en el puerto 9000...\n";

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
