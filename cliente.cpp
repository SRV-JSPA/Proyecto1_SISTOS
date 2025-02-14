#include <wx/wx.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <vector>
#include <thread>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class MyFrame : public wxFrame {
public:
    MyFrame() : wxFrame(nullptr, wxID_ANY, "Cliente WebSocket", wxDefaultPosition, wxSize(300, 150)) {
        wxPanel *panel = new wxPanel(this);
        wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

        wxStaticText *label = new wxStaticText(panel, wxID_ANY, "Ingrese su nombre:");
        sizer->Add(label, 0, wxALL | wxCENTER, 10);

        inputBox = new wxTextCtrl(panel, wxID_ANY);
        sizer->Add(inputBox, 0, wxALL | wxEXPAND, 10);

        wxButton *sendButton = new wxButton(panel, wxID_ANY, "Conectar y Enviar");
        sizer->Add(sendButton, 0, wxALL | wxCENTER, 10);

        sendButton->Bind(wxEVT_BUTTON, &MyFrame::OnSend, this);

        panel->SetSizer(sizer);
    }

private:
    wxTextCtrl *inputBox;

    void OnSend(wxCommandEvent &) {
        std::string usuario = inputBox->GetValue().ToStdString();
        if (usuario.empty()) {
            wxMessageBox("El nombre de usuario no puede estar vacío", "Error", wxOK | wxICON_ERROR);
            return;
        }

        try {
            net::io_context ioc;
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve("127.0.0.1", "9000");

            tcp::socket socket(ioc);
            net::connect(socket, results.begin(), results.end());

            websocket::stream<tcp::socket> ws(std::move(socket));
            ws.handshake("127.0.0.1", "/");

            std::vector<uint8_t> message;
            message.push_back(53);
            message.push_back(static_cast<uint8_t>(usuario.size()));
            message.insert(message.end(), usuario.begin(), usuario.end());

            ws.write(net::buffer(message));

            beast::flat_buffer buffer;
            ws.read(buffer);
            std::string response = beast::buffers_to_string(buffer.data());

            wxMessageBox("Respuesta del servidor:\n" + response, "Servidor", wxOK | wxICON_INFORMATION);

            std::thread([ws = std::move(ws)]() mutable {
                try {
                    beast::flat_buffer buffer;
                    while (true) {
                        buffer.clear();
                        ws.read(buffer);
                        std::string server_msg = beast::buffers_to_string(buffer.data());
                        std::cout << "Servidor dice: " << server_msg << std::endl;
                    }
                } catch (const std::exception &e) {
                    std::cerr << "Conexión cerrada: " << e.what() << std::endl;
                }
            }).detach();

        } catch (const std::exception &e) {
            wxMessageBox("Error: " + std::string(e.what()), "Error de conexión", wxOK | wxICON_ERROR);
        }
    }
};

class MyApp : public wxApp {
public:
    virtual bool OnInit() {
        MyFrame *frame = new MyFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
