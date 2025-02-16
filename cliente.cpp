#include <wx/wx.h>
#include <wx/listbox.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <vector>
#include <thread>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class ChatFrame : public wxFrame {
public:
    ChatFrame(std::shared_ptr<websocket::stream<tcp::socket>> ws, const std::string& usuario)
        : wxFrame(nullptr, wxID_ANY, "Chat", wxDefaultPosition, wxSize(400, 300)), ws_(ws), usuario_(usuario) {

        wxPanel* panel = new wxPanel(this);
        wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

        wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
        contactList = new wxListBox(panel, wxID_ANY);
        leftSizer->Add(contactList, 1, wxALL | wxEXPAND, 5);

        wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
        chatBox = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
        rightSizer->Add(chatBox, 1, wxALL | wxEXPAND, 5);

        messageInput = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        rightSizer->Add(messageInput, 0, wxALL | wxEXPAND, 5);

        wxButton* sendButton = new wxButton(panel, wxID_ANY, "Enviar");
        rightSizer->Add(sendButton, 0, wxALL | wxCENTER, 5);

        mainSizer->Add(leftSizer, 1, wxEXPAND);
        mainSizer->Add(rightSizer, 2, wxEXPAND);

        panel->SetSizer(mainSizer);

        sendButton->Bind(wxEVT_BUTTON, &ChatFrame::OnSend, this);
        messageInput->Bind(wxEVT_TEXT_ENTER, &ChatFrame::OnSend, this);

        StartReceivingMessages();
    }

private:
    wxListBox* contactList;
    wxTextCtrl* chatBox;
    wxTextCtrl* messageInput;
    std::shared_ptr<websocket::stream<tcp::socket>> ws_;
    std::string usuario_;

    void OnSend(wxCommandEvent&) {
        std::string message = messageInput->GetValue().ToStdString();
        if (message.empty()) return;

        try {
            std::vector<uint8_t> data;
            data.push_back(54); // Código de mensaje
            data.insert(data.end(), message.begin(), message.end());

            ws_->write(net::buffer(data));
            chatBox->AppendText("Yo: " + message + "\n");
            messageInput->Clear();
        } catch (const std::exception& e) {
            wxMessageBox("Error al enviar mensaje: " + std::string(e.what()), "Error", wxOK | wxICON_ERROR);
        }
    }

    void StartReceivingMessages() {
        std::thread([this]() {
            try {
                beast::flat_buffer buffer;
                while (true) {
                    buffer.clear();
                    ws_->read(buffer);
                    std::string server_msg = beast::buffers_to_string(buffer.data());

                    wxTheApp->CallAfter([this, server_msg]() {
                        chatBox->AppendText("Servidor: " + server_msg + "\n");
                    });
                }
            } catch (const std::exception& e) {
                wxTheApp->CallAfter([this, e]() {
                    wxMessageBox("Conexión cerrada: " + std::string(e.what()), "Error", wxOK | wxICON_ERROR);
                });
            }
        }).detach();
    }
};

class MyFrame : public wxFrame {
public:
    MyFrame() : wxFrame(nullptr, wxID_ANY, "Cliente WebSocket", wxDefaultPosition, wxSize(300, 150)) {
        wxPanel* panel = new wxPanel(this);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        wxStaticText* label = new wxStaticText(panel, wxID_ANY, "Ingrese su nombre:");
        sizer->Add(label, 0, wxALL | wxCENTER, 10);

        inputBox = new wxTextCtrl(panel, wxID_ANY);
        sizer->Add(inputBox, 0, wxALL | wxEXPAND, 10);

        wxButton* sendButton = new wxButton(panel, wxID_ANY, "Conectar");
        sizer->Add(sendButton, 0, wxALL | wxCENTER, 10);

        sendButton->Bind(wxEVT_BUTTON, &MyFrame::OnSend, this);

        panel->SetSizer(sizer);
    }

private:
    wxTextCtrl* inputBox;

    void OnSend(wxCommandEvent&) {
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

            auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));
            ws->handshake("127.0.0.1", "/");

            std::vector<uint8_t> message;
            message.push_back(53);
            message.push_back(static_cast<uint8_t>(usuario.size()));
            message.insert(message.end(), usuario.begin(), usuario.end());

            ws->write(net::buffer(message));

            beast::flat_buffer buffer;
            ws->read(buffer);
            std::string response = beast::buffers_to_string(buffer.data());

            wxMessageBox("Respuesta del servidor:\n" + response, "Servidor", wxOK | wxICON_INFORMATION);

            ChatFrame* chatFrame = new ChatFrame(ws, usuario);
            chatFrame->Show(true);
            Close();

        } catch (const std::exception& e) {
            wxMessageBox("Error: " + std::string(e.what()), "Error de conexión", wxOK | wxICON_ERROR);
        }
    }
};

class MyApp : public wxApp {
public:
    virtual bool OnInit() {
        MyFrame* frame = new MyFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(MyApp);
