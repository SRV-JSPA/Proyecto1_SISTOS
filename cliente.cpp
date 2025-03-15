#include <wx/wx.h>
#include <wx/listbox.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <unordered_map>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class ChatFrame;
class MyFrame;

enum class EstadoUsuario : uint8_t {
    DESCONECTADO = 0,
    ACTIVO = 1,
    OCUPADO = 2,
    INACTIVO = 3
};

class MyApp : public wxApp {
public:
    virtual bool OnInit() override;
};

wxIMPLEMENT_APP(MyApp);

class ChatFrame : public wxFrame {
public:
    ChatFrame(std::shared_ptr<websocket::stream<tcp::socket>> ws, const std::string& usuario);

private:
    wxListBox* contactList;
    wxTextCtrl* chatBox;
    wxTextCtrl* messageInput;
    std::shared_ptr<websocket::stream<tcp::socket>> ws_;
    std::string usuario_;
    std::string chatPartner_;
    std::unordered_map<std::string, std::vector<std::string>> chatHistory_;

    void AskForChatPartner();
    void LoadChatHistory();
    void OnSend(wxCommandEvent&);
    void StartReceivingMessages();
    void OnAddContact(wxCommandEvent&);
    void OnSelectContact(wxCommandEvent& evt);
};

ChatFrame::ChatFrame(std::shared_ptr<websocket::stream<tcp::socket>> ws, const std::string& usuario)
    : wxFrame(nullptr, wxID_ANY, "Chat", wxDefaultPosition, wxSize(500, 400)), ws_(ws), usuario_(usuario) {
    
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    contactList = new wxListBox(panel, wxID_ANY);
    leftSizer->Add(contactList, 1, wxALL | wxEXPAND, 5);

    wxButton* addContactButton = new wxButton(panel, wxID_ANY, "Agregar Contacto");
    leftSizer->Add(addContactButton, 0, wxALL | wxEXPAND, 5);

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
    addContactButton->Bind(wxEVT_BUTTON, &ChatFrame::OnAddContact, this);
    contactList->Bind(wxEVT_LISTBOX, &ChatFrame::OnSelectContact, this);

    StartReceivingMessages();
}

void ChatFrame::OnSend(wxCommandEvent&) {
    if (chatPartner_.empty()) {
        wxMessageBox("Seleccione un contacto primero", "Aviso", wxOK | wxICON_INFORMATION);
        return;
    }

    std::string message = messageInput->GetValue().ToStdString();
    if (message.empty()) return;

    try {
        std::string formatted_message = chatPartner_ + ":" + message;
        ws_->write(net::buffer(formatted_message));

        chatHistory_[chatPartner_].push_back(usuario_ + ": " + message);
        chatBox->AppendText(usuario_ + ": " + message + "\n");
        messageInput->Clear();
    } catch (const std::exception& e) {
        wxMessageBox("Error al enviar mensaje: " + std::string(e.what()), "Error", wxOK | wxICON_ERROR);
    }
}

void ChatFrame::StartReceivingMessages() {
    std::thread([this]() {
        try {
            while (true) {
                beast::flat_buffer buffer;
                ws_->read(buffer);
                std::string received_message = beast::buffers_to_string(buffer.data());

                wxGetApp().CallAfter([this, received_message]() {
                    chatBox->AppendText(received_message + "\n");
                });
            }
        } catch (const std::exception& e) {
            wxGetApp().CallAfter([this, e]() {
                wxMessageBox("Error en la conexión: " + std::string(e.what()), "Error", wxOK | wxICON_ERROR);
                Close();
            });
        }
    }).detach();
}

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
    void OnSend(wxCommandEvent&);
};

void MyFrame::OnSend(wxCommandEvent&) {
    std::string usuario = inputBox->GetValue().ToStdString();
    if (usuario.empty()) {
        wxMessageBox("El nombre de usuario no puede estar vacío", "Error", wxOK | wxICON_ERROR);
        return;
    }

    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve("3.13.27.172", "3000");

        tcp::socket socket(ioc);
        net::connect(socket, results.begin(), results.end());

        auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));
        ws->handshake("ws://3.13.27.172:3000", "/");

        ws->write(net::buffer(usuario));

        ChatFrame* chatFrame = new ChatFrame(ws, usuario);
        chatFrame->Show(true);
        Close();
    } catch (const std::exception& e) {
        wxMessageBox("Error: " + std::string(e.what()), "Error de conexión", wxOK | wxICON_ERROR);
    }
}

bool MyApp::OnInit() {
    MyFrame* frame = new MyFrame();
    frame->Show(true);
    return true;
}
