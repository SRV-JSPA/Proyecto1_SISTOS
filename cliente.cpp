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

private:
    wxButton* checkUserInfoButton;  
    
    void OnCheckUserInfo(wxCommandEvent&) {
        if (contactList->GetSelection() == wxNOT_FOUND) {
            wxMessageBox("Seleccione un usuario primero", "Aviso", wxOK | wxICON_INFORMATION);
            return;
        }

        std::string selectedUser = contactList->GetString(contactList->GetSelection()).ToStdString();
        
        try {
            std::vector<uint8_t> data;
            data.push_back(4); 
            
            data.push_back(static_cast<uint8_t>(selectedUser.length()));
            data.insert(data.end(), selectedUser.begin(), selectedUser.end());
            
            ws_->write(net::buffer(data));
        } catch (const std::exception& e) {
            wxMessageBox("Error al solicitar información: " + std::string(e.what()),
                        "Error", wxOK | wxICON_ERROR);
        }
    }
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

    checkUserInfoButton = new wxButton(panel, wxID_ANY, "Ver Info Usuario");
    leftSizer->Add(checkUserInfoButton, 0, wxALL | wxEXPAND, 5);

    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
    chatBox = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    rightSizer->Add(chatBox, 1, wxALL | wxEXPAND, 5);

    messageInput = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    rightSizer->Add(messageInput, 0, wxALL | wxEXPAND, 5);

    wxButton* sendButton = new wxButton(panel, wxID_ANY, "Enviar");
    rightSizer->Add(sendButton, 0, wxALL | wxCENTER, 5);

    mainSizer->Add(leftSizer, 1, wxEXPAND);
    mainSizer->Add(rightSizer, 2, wxEXPAND);

    checkUserInfoButton->Bind(wxEVT_BUTTON, &ChatFrame::OnCheckUserInfo, this);

    panel->SetSizer(mainSizer);

    sendButton->Bind(wxEVT_BUTTON, &ChatFrame::OnSend, this);
    messageInput->Bind(wxEVT_TEXT_ENTER, &ChatFrame::OnSend, this);
    addContactButton->Bind(wxEVT_BUTTON, &ChatFrame::OnAddContact, this);
    contactList->Bind(wxEVT_LISTBOX, &ChatFrame::OnSelectContact, this);

    StartReceivingMessages();
    AskForChatPartner();
}

void ChatFrame::AskForChatPartner() {
    try {
        beast::flat_buffer buffer;
        std::vector<uint8_t> request = {1};
        ws_->write(net::buffer(request));
    } catch (const std::exception& e) {
        wxMessageBox("Error al solicitar la lista de contactos: " + std::string(e.what()),
                    "Error", wxOK | wxICON_ERROR);
    }
}

void ChatFrame::LoadChatHistory() {
    if (chatPartner_.empty()) return;
    
    chatBox->Clear();
    if (chatHistory_.find(chatPartner_) != chatHistory_.end()) {
        for (const auto& message : chatHistory_[chatPartner_]) {
            chatBox->AppendText(message + "\n");
        }
    }
}

void ChatFrame::OnSend(wxCommandEvent&) {
    if (chatPartner_.empty()) {
        wxMessageBox("Seleccione un contacto primero", "Aviso", wxOK | wxICON_INFORMATION);
        return;
    }

    std::string message = messageInput->GetValue().ToStdString();
    if (message.empty()) return;

    try {
        std::vector<uint8_t> data;
        data.push_back(2); 
        
        data.push_back(static_cast<uint8_t>(usuario_.length()));
        data.insert(data.end(), usuario_.begin(), usuario_.end());
        
        data.push_back(static_cast<uint8_t>(chatPartner_.length()));
        data.insert(data.end(), chatPartner_.begin(), chatPartner_.end());
        
        uint16_t msgLen = static_cast<uint16_t>(message.length());
        data.push_back(static_cast<uint8_t>(msgLen >> 8));
        data.push_back(static_cast<uint8_t>(msgLen & 0xFF));
        data.insert(data.end(), message.begin(), message.end());

        ws_->write(net::buffer(data));

        std::string formatted_message = usuario_ + ": " + message;
        chatHistory_[chatPartner_].push_back(formatted_message);
        chatBox->AppendText(formatted_message + "\n");
        messageInput->Clear();
    } catch (const std::exception& e) {
        wxMessageBox("Error al enviar mensaje: " + std::string(e.what()),
                    "Error", wxOK | wxICON_ERROR);
    }
}

void ChatFrame::StartReceivingMessages() {
    std::thread([this]() {
        try {
            while (true) {
                beast::flat_buffer buffer;
                ws_->read(buffer);
                
                std::string data(static_cast<char*>(buffer.data().data()), buffer.data().size());
                
                if (!data.empty()) {
                    uint8_t code = data[0];
                    
                    switch (code) {
                        case 53: { 
                            uint8_t nameLen = data[1];
                            std::string name = data.substr(2, nameLen);
                            EstadoUsuario status = static_cast<EstadoUsuario>(data[2 + nameLen]);
                            
                            wxGetApp().CallAfter([this, name]() {
                                if (contactList->FindString(name) == wxNOT_FOUND) {
                                    contactList->Append(name);
                                }
                            });
                            break;
                        }
                        case 2: { 
                            uint8_t originLen = data[1];
                            std::string origin = data.substr(2, originLen);
                            
                            uint8_t destLen = data[2 + originLen];
                            std::string dest = data.substr(3 + originLen, destLen);
                            
                            uint16_t msgLen = (static_cast<uint16_t>(data[3 + originLen + destLen]) << 8) |
                                             static_cast<uint16_t>(data[4 + originLen + destLen]);
                            std::string message = data.substr(5 + originLen + destLen, msgLen);
                            
                            if (dest == usuario_ || origin == chatPartner_) {
                                std::string formatted_message = origin + ": " + message;
                                chatHistory_[origin].push_back(formatted_message);
                                
                                wxGetApp().CallAfter([this, formatted_message]() {
                                    chatBox->AppendText(formatted_message + "\n");
                                });
                            }
                            break;
                        }
                        case 5: { 
                            uint8_t nameLen = data[1];
                            std::string name = data.substr(2, nameLen);
                            EstadoUsuario status = static_cast<EstadoUsuario>(data[2 + nameLen]);
                            
                            wxGetApp().CallAfter([this, name, status]() {
                                std::string statusStr;
                                switch (status) {
                                    case EstadoUsuario::ACTIVO: statusStr = "Activo"; break;
                                    case EstadoUsuario::OCUPADO: statusStr = "Ocupado"; break;
                                    case EstadoUsuario::INACTIVO: statusStr = "Inactivo"; break;
                                    case EstadoUsuario::DESCONECTADO: statusStr = "Desconectado"; break;
                                }
                                
                                wxString info = wxString::Format(
                                    "Información del usuario %s:\n"
                                    "Estado: %s",
                                    name, statusStr
                                );
                                
                                wxMessageBox(info, "Información de Usuario", wxOK | wxICON_INFORMATION);
                            });
                            break;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            wxGetApp().CallAfter([this, e]() {
                wxMessageBox("Error en la conexión: " + std::string(e.what()),
                            "Error", wxOK | wxICON_ERROR);
                Close();
            });
        }
    }).detach();
}

void ChatFrame::OnAddContact(wxCommandEvent&) {
    wxTextEntryDialog dialog(this, "Ingrese el nombre del contacto:",
                           "Agregar Contacto", "");
    
    if (dialog.ShowModal() == wxID_OK) {
        std::string contactName = dialog.GetValue().ToStdString();
        if (!contactName.empty()) {
            contactList->Append(contactName);
        }
    }
}

void ChatFrame::OnSelectContact(wxCommandEvent& evt) {
    chatPartner_ = contactList->GetString(evt.GetSelection()).ToStdString();
    LoadChatHistory();
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
        std::string url = "ws://3.13.27.172:3000?name=" + usuario;
        ws->handshake(url, "/");

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