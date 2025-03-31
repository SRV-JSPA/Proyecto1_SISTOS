#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <deque>
#include <thread>
#include <chrono>
#include <fstream>
#include <ctime>
#include <iomanip>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

enum MessageType : uint8_t {
    CLIENT_LIST_USERS = 1,
    CLIENT_GET_USER = 2,
    CLIENT_CHANGE_STATUS = 3,
    CLIENT_SEND_MESSAGE = 4,
    CLIENT_GET_HISTORY = 5,

    SERVER_ERROR = 50,
    SERVER_LIST_USERS = 51,
    SERVER_USER_INFO = 52,
    SERVER_NEW_USER = 53,
    SERVER_STATUS_CHANGE = 54,
    SERVER_MESSAGE = 55,
    SERVER_HISTORY = 56
};

enum ErrorCode : uint8_t {
    ERROR_USER_NOT_FOUND = 1,
    ERROR_INVALID_STATUS = 2,
    ERROR_EMPTY_MESSAGE = 3,
    ERROR_DISCONNECTED_USER = 4
};

enum class EstadoUsuario : uint8_t {
    DESCONECTADO = 0,
    ACTIVO = 1,
    OCUPADO = 2,
    INACTIVO = 3
};

struct Mensaje {
    std::string origen;
    std::string destino;
    std::string contenido;
    std::chrono::system_clock::time_point timestamp;

    Mensaje(std::string org, std::string dest, std::string cont)
        : origen(std::move(org)), destino(std::move(dest)), contenido(std::move(cont)), 
        timestamp(std::chrono::system_clock::now()) {}
};

class Logger {
private:
    std::ofstream logFile;
    std::mutex logMutex;

public:
    Logger(const std::string& filename) {
        logFile.open(filename, std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file: " << filename << std::endl;
        }
    }

    ~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "] " << message;
        
        if (logFile.is_open()) {
            logFile << ss.str() << std::endl;
        }
        std::cout << ss.str() << std::endl;
    }
};

class Usuario {
public:
    std::string nombre;
    EstadoUsuario estado;
    std::shared_ptr<websocket::stream<tcp::socket>> ws_stream;
    std::deque<Mensaje> historial_mensajes;
    std::chrono::system_clock::time_point ultima_actividad;
    net::ip::address ip_address;

    Usuario(std::string nombre, std::shared_ptr<websocket::stream<tcp::socket>> ws, 
            net::ip::address ip)
        : nombre(std::move(nombre)), 
          estado(EstadoUsuario::ACTIVO), 
          ws_stream(ws),
          ultima_actividad(std::chrono::system_clock::now()),
          ip_address(ip) {}

    bool esta_activo() const {
        return estado == EstadoUsuario::ACTIVO;
    }
    
    bool puede_recibir_mensajes() const {
        return estado != EstadoUsuario::DESCONECTADO && estado != EstadoUsuario::OCUPADO;
    }
    
    void actualizar_actividad() {
        ultima_actividad = std::chrono::system_clock::now();
    }
};

class ChatServer {
private:
    std::unordered_map<std::string, std::shared_ptr<Usuario>> usuarios;
    std::mutex usuarios_mutex;
    std::deque<Mensaje> chat_general;
    std::mutex chat_general_mutex;
    Logger logger;
    std::chrono::seconds timeout_inactividad;
    std::atomic<bool> running;

    void check_inactivity() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(10)); 
            
            auto ahora = std::chrono::system_clock::now();
            std::lock_guard<std::mutex> lock(usuarios_mutex);
            
            for (auto& [nombre, usuario] : usuarios) {
                if (usuario->estado == EstadoUsuario::ACTIVO) {
                    auto tiempo_inactivo = std::chrono::duration_cast<std::chrono::seconds>(
                        ahora - usuario->ultima_actividad);
                    
                    if (tiempo_inactivo > timeout_inactividad) {
                        usuario->estado = EstadoUsuario::INACTIVO;
                        logger.log("Usuario " + nombre + " cambiado a INACTIVO por timeout");

                        std::vector<uint8_t> notificacion = crear_mensaje_cambio_estado(nombre, usuario->estado);
                        broadcast_mensaje(notificacion);
                    }
                }
            }
        }
    }

    std::vector<uint8_t> crear_mensaje_error(ErrorCode codigo) {
        std::vector<uint8_t> mensaje = {SERVER_ERROR, static_cast<uint8_t>(codigo)};
        return mensaje;
    }

    std::vector<uint8_t> crear_mensaje_lista_usuarios() {
        std::lock_guard<std::mutex> lock(usuarios_mutex);

        uint8_t count = 0;
        for (const auto& [nombre, usuario] : usuarios) {
            if (usuario->estado != EstadoUsuario::DESCONECTADO) {
                count++;
            }
        }

        std::vector<uint8_t> mensaje = {SERVER_LIST_USERS, count};
        
        for (const auto& [nombre, usuario] : usuarios) {
            if (usuario->estado != EstadoUsuario::DESCONECTADO) {
                mensaje.push_back(static_cast<uint8_t>(nombre.size()));
                mensaje.insert(mensaje.end(), nombre.begin(), nombre.end());
                mensaje.push_back(static_cast<uint8_t>(usuario->estado));
            }
        }
        
        return mensaje;
    }

    std::vector<uint8_t> crear_mensaje_info_usuario(const std::string& nombre) {
        std::lock_guard<std::mutex> lock(usuarios_mutex);
        
        auto it = usuarios.find(nombre);
        if (it == usuarios.end() || it->second->estado == EstadoUsuario::DESCONECTADO) {
            return crear_mensaje_error(ERROR_USER_NOT_FOUND);
        }
        
        std::vector<uint8_t> mensaje = {SERVER_USER_INFO, static_cast<uint8_t>(nombre.size())};
        mensaje.insert(mensaje.end(), nombre.begin(), nombre.end());
        mensaje.push_back(static_cast<uint8_t>(it->second->estado));
        
        return mensaje;
    }

    std::vector<uint8_t> crear_mensaje_recibido(const std::string& origen, const std::string& contenido) {
        std::vector<uint8_t> mensaje = {
            SERVER_MESSAGE, 
            static_cast<uint8_t>(origen.size())
        };
        mensaje.insert(mensaje.end(), origen.begin(), origen.end());
        mensaje.push_back(static_cast<uint8_t>(contenido.size()));
        mensaje.insert(mensaje.end(), contenido.begin(), contenido.end());
        
        return mensaje;
    }

    std::vector<uint8_t> crear_mensaje_historial(const std::string& chat) {
        std::vector<std::shared_ptr<Mensaje>> historial;

        if (chat == "~") {
            std::lock_guard<std::mutex> lock(chat_general_mutex);

            size_t count = std::min(chat_general.size(), size_t(255));
            historial.reserve(count);
            
            if (!chat_general.empty()) {
                auto it = chat_general.end() - std::min(chat_general.size(), size_t(count));
                for (size_t i = 0; i < std::min(chat_general.size(), size_t(count)); i++) {
                    historial.push_back(std::make_shared<Mensaje>(*it++));
                }
            }
        } else {
            std::lock_guard<std::mutex> lock(usuarios_mutex);
            
            auto it = usuarios.find(chat);
            if (it == usuarios.end()) {
                return crear_mensaje_error(ERROR_USER_NOT_FOUND);
            }

            size_t count = std::min(it->second->historial_mensajes.size(), size_t(255));
            historial.reserve(count);
            
            if (!it->second->historial_mensajes.empty()) {
                auto msg_it = it->second->historial_mensajes.end() - std::min(it->second->historial_mensajes.size(), size_t(count));
                for (size_t i = 0; i < std::min(it->second->historial_mensajes.size(), size_t(count)); i++) {
                    historial.push_back(std::make_shared<Mensaje>(*msg_it++));
                }
            }
        }

        std::vector<uint8_t> mensaje = {SERVER_HISTORY, static_cast<uint8_t>(historial.size())};
        
        for (const auto& msg : historial) {
            mensaje.push_back(static_cast<uint8_t>(msg->origen.size()));
            mensaje.insert(mensaje.end(), msg->origen.begin(), msg->origen.end());
            mensaje.push_back(static_cast<uint8_t>(msg->contenido.size()));
            mensaje.insert(mensaje.end(), msg->contenido.begin(), msg->contenido.end());
        }
        
        return mensaje;
    }

public:
    ChatServer() 
        : logger("chat_server.log"), 
          timeout_inactividad(60),
          running(true) {

        std::thread inactivity_thread(&ChatServer::check_inactivity, this);
        inactivity_thread.detach();
    }
    
    ~ChatServer() {
        running = false;
    }

    std::mutex& get_usuarios_mutex() {
        return usuarios_mutex;
    }
    
    std::unordered_map<std::string, std::shared_ptr<Usuario>>& get_usuarios() {
        return usuarios;
    }

    void broadcast_mensaje(const std::vector<uint8_t>& mensaje) {
        std::lock_guard<std::mutex> lock(usuarios_mutex);
        for (auto& [nombre, usuario] : usuarios) {
            if (usuario->estado != EstadoUsuario::DESCONECTADO) {
                try {
                    usuario->ws_stream->write(net::buffer(mensaje));
                } catch (const std::exception& e) {
                    logger.log("Error enviando broadcast a " + nombre + ": " + e.what());
                }
            }
        }
    }

    bool enviar_mensaje_a_usuario(const std::string& nombre_usuario, const std::vector<uint8_t>& mensaje) {
        std::lock_guard<std::mutex> lock(usuarios_mutex);
        auto it = usuarios.find(nombre_usuario);
        if (it != usuarios.end() && it->second->estado != EstadoUsuario::DESCONECTADO) {
            try {
                it->second->ws_stream->write(net::buffer(mensaje));
                return true;
            } catch (const std::exception& e) {
                logger.log("Error enviando mensaje a " + nombre_usuario + ": " + e.what());
            }
        }
        return false;
    }

    std::string parse_nombre_usuario(const std::string& query_string) {
        std::string nombre;
        if (query_string.find("name=") != std::string::npos) {
            nombre = query_string.substr(query_string.find("name=") + 5);
            size_t pos = nombre.find('&');
            if (pos != std::string::npos) {
                nombre = nombre.substr(0, pos);
            }
            boost::replace_all(nombre, "%20", " ");
        }
        return nombre;
    }

    std::vector<uint8_t> crear_mensaje_cambio_estado(const std::string& nombre, EstadoUsuario estado) {
        std::vector<uint8_t> mensaje = {SERVER_STATUS_CHANGE, static_cast<uint8_t>(nombre.size())};
        mensaje.insert(mensaje.end(), nombre.begin(), nombre.end());
        mensaje.push_back(static_cast<uint8_t>(estado));
        
        return mensaje;
    }

    void procesar_listar_usuarios(const std::string& nombre_cliente) {
        logger.log("Cliente " + nombre_cliente + " solicita lista de usuarios");
        auto mensaje = crear_mensaje_lista_usuarios();
        enviar_mensaje_a_usuario(nombre_cliente, mensaje);
    }

    void procesar_obtener_usuario(const std::string& nombre_cliente, const std::vector<uint8_t>& datos) {
        if (datos.size() < 2) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_USER_NOT_FOUND));
            return;
        }
        
        uint8_t len = datos[1];
        if (datos.size() < 2 + len) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_USER_NOT_FOUND));
            return;
        }
        
        std::string nombre_buscado(datos.begin() + 2, datos.begin() + 2 + len);
        logger.log("Cliente " + nombre_cliente + " solicita info de usuario " + nombre_buscado);
        
        auto mensaje = crear_mensaje_info_usuario(nombre_buscado);
        enviar_mensaje_a_usuario(nombre_cliente, mensaje);
    }

    void procesar_cambiar_estado(const std::string& nombre_cliente, const std::vector<uint8_t>& datos) {
        if (datos.size() < 3) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_INVALID_STATUS));
            return;
        }
        
        uint8_t len = datos[1];
        if (datos.size() < 2 + len + 1) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_INVALID_STATUS));
            return;
        }
        
        std::string nombre_usuario(datos.begin() + 2, datos.begin() + 2 + len);
        uint8_t estado = datos[2 + len];
        
        if (estado > 3) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_INVALID_STATUS));
            return;
        }
        
        logger.log("Cliente " + nombre_cliente + " solicita cambiar estado de " + 
                   nombre_usuario + " a " + std::to_string(estado));
    
        if (nombre_cliente != nombre_usuario) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_USER_NOT_FOUND));
            return;
        }
        
        std::lock_guard<std::mutex> lock(usuarios_mutex);
        auto it = usuarios.find(nombre_usuario);
        if (it == usuarios.end() || it->second->estado == EstadoUsuario::DESCONECTADO) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_USER_NOT_FOUND));
            return;
        }
    
        it->second->estado = static_cast<EstadoUsuario>(estado);
        it->second->actualizar_actividad();
    
        auto mensaje = crear_mensaje_cambio_estado(nombre_usuario, it->second->estado);
        broadcast_mensaje(mensaje);
        
        enviar_mensaje_a_usuario(nombre_cliente, mensaje);
    }

    void procesar_enviar_mensaje(const std::string& nombre_cliente, const std::vector<uint8_t>& datos) {
        if (datos.size() < 2) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_EMPTY_MESSAGE));
            return;
        }
        
        uint8_t len_dest = datos[1];
        if (datos.size() < 2 + len_dest + 1) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_EMPTY_MESSAGE));
            return;
        }
        
        std::string destino(datos.begin() + 2, datos.begin() + 2 + len_dest);
        
        uint8_t len_msg = datos[2 + len_dest];
        if (datos.size() < 3 + len_dest + len_msg) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_EMPTY_MESSAGE));
            return;
        }
        
        std::string contenido(datos.begin() + 3 + len_dest, datos.begin() + 3 + len_dest + len_msg);
        
        if (contenido.empty()) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_EMPTY_MESSAGE));
            return;
        }
        
        logger.log("Cliente " + nombre_cliente + " envía mensaje a " + destino + ": " + contenido);

        {
            std::lock_guard<std::mutex> lock(usuarios_mutex);
            auto it = usuarios.find(nombre_cliente);
            if (it != usuarios.end()) {
                it->second->actualizar_actividad();
            }
        }

        auto mensaje_respuesta = crear_mensaje_recibido(nombre_cliente, contenido);

        if (destino == "~") {
            {
                std::lock_guard<std::mutex> lock(chat_general_mutex);
                chat_general.emplace_back(nombre_cliente, destino, contenido);
                if (chat_general.size() > 1000) {
                    chat_general.pop_front();
                }
            }

            broadcast_mensaje(mensaje_respuesta);
        } else {
            bool enviado = false;
            {
                std::lock_guard<std::mutex> lock(usuarios_mutex);

                auto it_dest = usuarios.find(destino);
                if (it_dest == usuarios.end() || it_dest->second->estado == EstadoUsuario::DESCONECTADO) {
                    enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_DISCONNECTED_USER));
                    return;
                }

                auto it_origen = usuarios.find(nombre_cliente);
                if (it_origen != usuarios.end()) {
                    it_origen->second->historial_mensajes.emplace_back(nombre_cliente, destino, contenido);
                    if (it_origen->second->historial_mensajes.size() > 1000) {
                        it_origen->second->historial_mensajes.pop_front();
                    }
                }

                it_dest->second->historial_mensajes.emplace_back(nombre_cliente, destino, contenido);
                if (it_dest->second->historial_mensajes.size() > 1000) {
                    it_dest->second->historial_mensajes.pop_front();
                }

                if (it_dest->second->puede_recibir_mensajes()) {
                    try {
                        it_dest->second->ws_stream->write(net::buffer(mensaje_respuesta));
                        enviado = true;
                    } catch (...) {}
                }
            }
            
            enviar_mensaje_a_usuario(nombre_cliente, mensaje_respuesta);
            
            logger.log("Mensaje de " + nombre_cliente + " a " + destino + 
                       (enviado ? " enviado" : " no enviado (usuario ocupado)"));
        }
    }
    
    void procesar_obtener_historial(const std::string& nombre_cliente, const std::vector<uint8_t>& datos) {
        if (datos.size() < 2) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_USER_NOT_FOUND));
            return;
        }
        
        uint8_t len = datos[1];
        if (datos.size() < 2 + len) {
            enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_USER_NOT_FOUND));
            return;
        }
        
        std::string chat(datos.begin() + 2, datos.begin() + 2 + len);
        logger.log("Cliente " + nombre_cliente + " solicita historial de chat " + chat);

        if (chat != "~") {
            std::lock_guard<std::mutex> lock(usuarios_mutex);
            if (usuarios.find(chat) == usuarios.end()) {
                enviar_mensaje_a_usuario(nombre_cliente, crear_mensaje_error(ERROR_USER_NOT_FOUND));
                return;
            }
        }
        
        auto mensaje = crear_mensaje_historial(chat);
        enviar_mensaje_a_usuario(nombre_cliente, mensaje);
    }

    void set_timeout_inactividad(int seconds) {
        timeout_inactividad = std::chrono::seconds(seconds);
        logger.log("Timeout de inactividad establecido a " + std::to_string(seconds) + " segundos");
    }

    void manejar_conexion(tcp::socket socket, const std::string& query_string) {
        try {
            std::string nombre_usuario = parse_nombre_usuario(query_string);

            if (nombre_usuario.empty()) {
                logger.log("Conexión rechazada: nombre de usuario vacío");

                http::response<http::string_body> 
                res{http::status::bad_request, 11};
                res.set(http::field::server, "ChatServer");
                res.set(http::field::content_type, "text/plain");
                res.body() = "Nombre de usuario vacío";
                res.prepare_payload();
                
                http::write(socket, res);
                return;
            }
            
            if (nombre_usuario == "~") {
                logger.log("Conexión rechazada: nombre de usuario reservado");

                http::response<http::string_body> res{http::status::bad_request, 11};
                res.set(http::field::server, "ChatServer");
                res.set(http::field::content_type, "text/plain");
                res.body() = "Nombre de usuario reservado";
                res.prepare_payload();
                
                http::write(socket, res);
                return;
            }
            
            {
                std::lock_guard<std::mutex> lock(usuarios_mutex);
                auto it = usuarios.find(nombre_usuario);
                if (it != usuarios.end() && it->second->estado != EstadoUsuario::DESCONECTADO) {
                    logger.log("Conexión rechazada: usuario ya conectado: " + nombre_usuario);

                    http::response<http::string_body> res{http::status::bad_request, 11};
                    res.set(http::field::server, "ChatServer");
                    res.set(http::field::content_type, "text/plain");
                    res.body() = "Usuario ya conectado";
                    res.prepare_payload();
                    
                    http::write(socket, res);
                    return;
                }
            }

            auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));

            net::ip::address ip_address = ws->next_layer().remote_endpoint().address();

            ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

            try {
                ws->accept();
                logger.log("WebSocket handshake aceptado para: " + nombre_usuario);
            } 
            catch (const std::exception& e) {
                logger.log("Error en WebSocket handshake para " + nombre_usuario + ": " + e.what());
                return;
            }

            logger.log("Conexión aceptada: " + nombre_usuario + " desde " + ip_address.to_string());
            
            {
                std::lock_guard<std::mutex> lock(usuarios_mutex);
                auto it = usuarios.find(nombre_usuario);
                if (it != usuarios.end()) {
                    it->second->ws_stream = ws;
                    it->second->estado = EstadoUsuario::ACTIVO;
                    it->second->actualizar_actividad();
                    it->second->ip_address = ip_address;
                } else {
                    usuarios[nombre_usuario] = std::make_shared<Usuario>(nombre_usuario, ws, ip_address);
                }
            }

            std::vector<uint8_t> notificacion = {
                SERVER_NEW_USER, 
                static_cast<uint8_t>(nombre_usuario.size())
            };
            notificacion.insert(notificacion.end(), nombre_usuario.begin(), nombre_usuario.end());
            notificacion.push_back(static_cast<uint8_t>(EstadoUsuario::ACTIVO));
            
            broadcast_mensaje(notificacion);

            beast::flat_buffer buffer;
            
            while (true) {
                try {
                    ws->read(buffer);
                    
                    auto data = beast::buffers_to_string(buffer.data());
                    std::vector<uint8_t> datos(data.begin(), data.end());
                    buffer.consume(buffer.size());
                    
                    if (datos.empty()) {
                        continue;
                    }
                    
                    switch (datos[0]) {
                        case CLIENT_LIST_USERS:
                            procesar_listar_usuarios(nombre_usuario);
                            break;
                            
                        case CLIENT_GET_USER:
                            procesar_obtener_usuario(nombre_usuario, datos);
                            break;
                            
                        case CLIENT_CHANGE_STATUS:
                            procesar_cambiar_estado(nombre_usuario, datos);
                            break;
                            
                        case CLIENT_SEND_MESSAGE:
                            procesar_enviar_mensaje(nombre_usuario, datos);
                            break;
                            
                        case CLIENT_GET_HISTORY:
                            procesar_obtener_historial(nombre_usuario, datos);
                            break;
                            
                        default:
                            logger.log("Mensaje desconocido de " + nombre_usuario + ": tipo " + 
                                      std::to_string(datos[0]));
                            break;
                    }
                    
                } catch (const beast::error_code& ec) {
                    if (ec == websocket::error::closed) {
                        logger.log("Conexión cerrada por cliente: " + nombre_usuario);
                        break;
                    } else {
                        logger.log("Error leyendo de cliente " + nombre_usuario + ": " + ec.message());
                        break;
                    }
                } catch (const std::exception& e) {
                    logger.log("Error procesando mensaje de " + nombre_usuario + ": " + e.what());
                    break;
                }
            }

            {
                std::lock_guard<std::mutex> lock(usuarios_mutex);
                auto it = usuarios.find(nombre_usuario);
                if (it != usuarios.end()) {
                    it->second->estado = EstadoUsuario::DESCONECTADO;
                    logger.log("Usuario " + nombre_usuario + " marcado como DESCONECTADO");
                }
            }

            std::vector<uint8_t> notificacion_desconexion = crear_mensaje_cambio_estado(
                nombre_usuario, EstadoUsuario::DESCONECTADO);
            broadcast_mensaje(notificacion_desconexion);
            
        } catch (const std::exception& e) {
            logger.log("Error en manejo de conexión: " + std::string(e.what()));
        }
    }
};

std::string extract_query_string(boost::string_view target) {
    auto pos = target.find('?');
    if (pos != boost::string_view::npos) {
        return std::string(target.substr(pos + 1));
    }
    return "";
}


int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Uso: " << argv[0] << " <puerto>" << std::endl;
            return 1;
        }
        
        int puerto = std::stoi(argv[1]);
        
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {tcp::v4(), static_cast<unsigned short>(puerto)}};
        acceptor.set_option(boost::asio::socket_base::reuse_address(true));
        
        std::cout << "Servidor iniciado en puerto " << puerto << std::endl;
        
        ChatServer servidor;
        servidor.set_timeout_inactividad(120);
        

        while (true) {

            tcp::socket socket{ioc};
            acceptor.accept(socket);
            
            auto endpoint = socket.remote_endpoint();
            std::cout << "Nueva conexión desde " << endpoint.address().to_string() 
                      << ":" << endpoint.port() << std::endl;
            
            socket.set_option(tcp::socket::keep_alive(true));
            

            std::thread([&servidor, sock = std::move(socket)]() mutable {
                try {

                    beast::flat_buffer buffer;
                    http::request<http::string_body> req;
                    

                    http::read(sock, buffer, req);
                    
                    std::cout << "Thread: Petición HTTP recibida: " << req.target() << std::endl;
                    std::string query_string = extract_query_string(req.target());
                    std::cout << "Thread: Query string: " << query_string << std::endl;
                    

                    std::string nombre_usuario = servidor.parse_nombre_usuario(query_string);
                    std::cout << "Thread: Procesando conexión para usuario: " << nombre_usuario << std::endl;
                    

                    if (nombre_usuario.empty()) {
                        http::response<http::string_body> res{http::status::bad_request, 11};
                        res.set(http::field::server, "ChatServer");
                        res.set(http::field::content_type, "text/plain");
                        res.body() = "Nombre de usuario vacío";
                        res.prepare_payload();
                        http::write(sock, res);
                        std::cout << "Thread: Conexión rechazada: nombre de usuario vacío" << std::endl;
                        return;
                    }
                    
                    if (nombre_usuario == "~") {
                        http::response<http::string_body> res{http::status::bad_request, 11};
                        res.set(http::field::server, "ChatServer");
                        res.set(http::field::content_type, "text/plain");
                        res.body() = "Nombre de usuario reservado";
                        res.prepare_payload();
                        http::write(sock, res);
                        std::cout << "Thread: Conexión rechazada: nombre de usuario reservado" << std::endl;
                        return;
                    }
                    

                    bool usuario_ya_conectado = false;
                    {
                        std::lock_guard<std::mutex> lock(servidor.get_usuarios_mutex());
                        auto& usuarios = servidor.get_usuarios();
                        auto it = usuarios.find(nombre_usuario);
                        if (it != usuarios.end() && it->second->estado != EstadoUsuario::DESCONECTADO) {
                            usuario_ya_conectado = true;
                        }
                    }
                    
                    if (usuario_ya_conectado) {
                        http::response<http::string_body> res{http::status::bad_request, 11};
                        res.set(http::field::server, "ChatServer");
                        res.set(http::field::content_type, "text/plain");
                        res.body() = "Usuario ya conectado";
                        res.prepare_payload();
                        http::write(sock, res);
                        std::cout << "Thread: Conexión rechazada: usuario ya conectado: " << nombre_usuario << std::endl;
                        return;
                    }
                    

                    auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(sock));
                    ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
                    
                    try {

                        ws->accept(req);
                        std::cout << "Thread: WebSocket handshake aceptado para: " << nombre_usuario << std::endl;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Thread: Error aceptando WebSocket para " << nombre_usuario 
                                  << ": " << e.what() << std::endl;
                        return;
                    }
                    

                    net::ip::address ip_address;
                    try {
                        ip_address = ws->next_layer().remote_endpoint().address();
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Thread: Error obteniendo IP para " << nombre_usuario 
                                  << ": " << e.what() << std::endl;

                    }

                    {
                        std::lock_guard<std::mutex> lock(servidor.get_usuarios_mutex());
                        auto& usuarios = servidor.get_usuarios();
                        auto it = usuarios.find(nombre_usuario);
                        if (it != usuarios.end()) {
                            it->second->ws_stream = ws;
                            it->second->estado = EstadoUsuario::ACTIVO;
                            it->second->actualizar_actividad();
                            it->second->ip_address = ip_address;
                        } else {
                            usuarios[nombre_usuario] = std::make_shared<Usuario>(nombre_usuario, ws, ip_address);
                        }
                    }
                    

                    std::vector<uint8_t> notificacion = {
                        SERVER_NEW_USER, 
                        static_cast<uint8_t>(nombre_usuario.size())
                    };
                    notificacion.insert(notificacion.end(), nombre_usuario.begin(), nombre_usuario.end());
                    notificacion.push_back(static_cast<uint8_t>(EstadoUsuario::ACTIVO));
                    
                    servidor.broadcast_mensaje(notificacion);
                    std::cout << "Thread: Usuario " << nombre_usuario << " conectado y notificado" << std::endl;

                    beast::flat_buffer msg_buffer;
                    
                    while (true) {
                        try {
                            ws->read(msg_buffer);
                            
                            auto data = beast::buffers_to_string(msg_buffer.data());
                            std::vector<uint8_t> datos(data.begin(), data.end());
                            msg_buffer.consume(msg_buffer.size());
                            
                            if (datos.empty()) {
                                continue;
                            }
                            
                            switch (datos[0]) {
                                case CLIENT_LIST_USERS:
                                    servidor.procesar_listar_usuarios(nombre_usuario);
                                    break;
                                    
                                case CLIENT_GET_USER:
                                    servidor.procesar_obtener_usuario(nombre_usuario, datos);
                                    break;
                                    
                                case CLIENT_CHANGE_STATUS:
                                    servidor.procesar_cambiar_estado(nombre_usuario, datos);
                                    break;
                                    
                                case CLIENT_SEND_MESSAGE:
                                    servidor.procesar_enviar_mensaje(nombre_usuario, datos);
                                    break;
                                    
                                case CLIENT_GET_HISTORY:
                                    servidor.procesar_obtener_historial(nombre_usuario, datos);
                                    break;
                                    
                                default:
                                    std::cout << "Thread " << nombre_usuario << ": Mensaje desconocido tipo " 
                                              << (int)datos[0] << std::endl;
                                    break;
                            }
                            
                        } catch (const beast::error_code& ec) {
                            if (ec == websocket::error::closed) {
                                std::cout << "Thread: Conexión cerrada por cliente: " << nombre_usuario << std::endl;
                                break;
                            } else {
                                std::cout << "Thread: Error leyendo de cliente " << nombre_usuario << ": " << ec.message() << std::endl;
                                break;
                            }
                        } catch (const std::exception& e) {
                            std::cout << "Thread: Error procesando mensaje de " << nombre_usuario << ": " << e.what() << std::endl;
                            break;
                        }
                    }
                    

                    {
                        std::lock_guard<std::mutex> lock(servidor.get_usuarios_mutex());
                        auto& usuarios = servidor.get_usuarios();
                        auto it = usuarios.find(nombre_usuario);
                        if (it != usuarios.end()) {
                            it->second->estado = EstadoUsuario::DESCONECTADO;
                            std::cout << "Thread: Usuario " << nombre_usuario << " marcado como DESCONECTADO" << std::endl;
                        }
                    }
                    

                    std::vector<uint8_t> notificacion_desconexion = servidor.crear_mensaje_cambio_estado(
                        nombre_usuario, EstadoUsuario::DESCONECTADO);
                    servidor.broadcast_mensaje(notificacion_desconexion);
                    
                } catch (const std::exception& e) {
                    std::cerr << "Thread: Error en manejo de conexión: " << e.what() << std::endl;
                }
            }).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error en el servidor: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}