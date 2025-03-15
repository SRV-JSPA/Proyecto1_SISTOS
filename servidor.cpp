#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <deque>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct Mensaje {
    std::string origen;
    std::string destino;
    std::string contenido;
    Mensaje(std::string org, std::string dest, std::string cont)
        : origen(std::move(org)), destino(std::move(dest)), contenido(std::move(cont)) {}
};

enum class EstadoUsuario : uint8_t {
    DESCONECTADO = 0,
    ACTIVO = 1,
    OCUPADO = 2,
    INACTIVO = 3
};

class Usuario {
public:
    std::string nombre;
    EstadoUsuario estado;
    std::shared_ptr<websocket::stream<tcp::socket>> ws_stream;
    std::deque<Mensaje> historial_mensajes;

    Usuario(std::string nombre, std::shared_ptr<websocket::stream<tcp::socket>> ws)
        : nombre(std::move(nombre)), estado(EstadoUsuario::ACTIVO), ws_stream(ws) {}

    bool esta_activo() const {
        return estado == EstadoUsuario::ACTIVO;
    }
};

class ChatServer {
private:
    std::unordered_map<std::string, std::shared_ptr<Usuario>> usuarios;
    std::mutex usuarios_mutex;

    void broadcast_mensaje(const std::vector<uint8_t>& mensaje) {
        std::lock_guard<std::mutex> lock(usuarios_mutex);
        for (auto& [nombre, usuario] : usuarios) {
            if (usuario->esta_activo()) {
                try {
                    usuario->ws_stream->write(net::buffer(mensaje));
                } catch (...) {}
            }
        }
    }

public:
    void manejar_conexion(tcp::socket socket) {
        try {
            auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));
            ws->accept();

            beast::flat_buffer buffer;
            ws->read(buffer);
            std::string nombre_usuario = beast::buffers_to_string(buffer.data());

            {
                std::lock_guard<std::mutex> lock(usuarios_mutex);
                if (usuarios.find(nombre_usuario) != usuarios.end()) {
                    return;
                }
                usuarios[nombre_usuario] = std::make_shared<Usuario>(nombre_usuario, ws);
            }

            std::vector<uint8_t> mensaje_nuevo = {53, static_cast<uint8_t>(nombre_usuario.size())};
            mensaje_nuevo.insert(mensaje_nuevo.end(), nombre_usuario.begin(), nombre_usuario.end());
            mensaje_nuevo.push_back(static_cast<uint8_t>(EstadoUsuario::ACTIVO));
            broadcast_mensaje(mensaje_nuevo);

            while (true) {
                beast::flat_buffer buffer;
                ws->read(buffer);
                std::string mensaje = beast::buffers_to_string(buffer.data());

                size_t separator = mensaje.find(":");
                if (separator != std::string::npos) {
                    std::string destinatario = mensaje.substr(0, separator);
                    std::string contenido = mensaje.substr(separator + 1);

                    std::lock_guard<std::mutex> lock(usuarios_mutex);
                    if (usuarios.find(destinatario) != usuarios.end()) {
                        auto& usuario_destino = usuarios[destinatario];
                        usuario_destino->ws_stream->write(net::buffer(contenido));
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error en conexión WebSocket: " << e.what() << std::endl;
        }
    }
};

int main() {
    try {
        net::io_context ioc;
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 3000));
        acceptor.set_option(boost::asio::socket_base::reuse_address(true));

        ChatServer servidor;

        while (true) {
            tcp::socket socket(ioc);
            acceptor.accept(socket);
            std::thread([&servidor](tcp::socket sock) {
                servidor.manejar_conexion(std::move(sock));
            }, std::move(socket)).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error en el servidor: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
