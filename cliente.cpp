#include <wx/wx.h>
#include <wx/listbox.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <vector>
#include <thread>
#include <unordered_map>
#include <mutex>

namespace beast = boost::beast;
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

class ContactInfo {
public:
    std::string nombre;
    EstadoUsuario estado;

    ContactInfo() : nombre(""), estado(EstadoUsuario::DESCONECTADO) {}
    
    ContactInfo(std::string n, EstadoUsuario e) : nombre(std::move(n)), estado(e) {}
    
    wxString FormatName() const {
        std::string statusIndicator;
        switch (estado) {
            case EstadoUsuario::ACTIVO: statusIndicator = "[A] "; break;
            case EstadoUsuario::OCUPADO: statusIndicator = "[O] "; break;
            case EstadoUsuario::INACTIVO: statusIndicator = "[I] "; break;
            case EstadoUsuario::DESCONECTADO: statusIndicator = "[D] "; break;
        }
        return wxString(statusIndicator + nombre);
    }
};

class ChatFrame;
class MyFrame;

class MyApp : public wxApp {
public:
    virtual bool OnInit() override;
};

wxIMPLEMENT_APP(MyApp);


enum {
    ID_CHAT_TITLE = wxID_HIGHEST + 1
};

class ChatFrame : public wxFrame {
public:
    ChatFrame(std::shared_ptr<websocket::stream<tcp::socket>> ws, const std::string& usuario);
    ~ChatFrame();

private:
    wxListBox* contactList;
    wxTextCtrl* chatBox;
    wxTextCtrl* messageInput;
    wxButton* sendButton;
    wxButton* addContactButton;
    wxButton* checkUserInfoButton;
    wxButton* refreshUsersButton;
    wxChoice* statusChoice;
    wxStaticText* chatTitle;
    wxStaticText* statusText;
    
    std::shared_ptr<websocket::stream<tcp::socket>> ws_;
    std::string usuario_;
    std::string chatPartner_;
    bool running_;
    std::mutex chatHistoryMutex_;
    EstadoUsuario currentStatus_;
    bool canSendMessages_;
    bool forceCanSend_;

    std::unordered_map<std::string, ContactInfo> contacts_;
    std::unordered_map<std::string, std::vector<std::string>> chatHistory_;
    
    void RequestUserList();
    void LoadChatHistory();
    void RequestChatHistory();
    void OnSend(wxCommandEvent&);
    void StartReceivingMessages();
    void OnAddContact(wxCommandEvent&);
    void OnSelectContact(wxCommandEvent& evt);
    void OnCheckUserInfo(wxCommandEvent&);
    void OnRefreshUsers(wxCommandEvent&);
    void OnChangeStatus(wxCommandEvent&);

    std::vector<uint8_t> CreateListUsersMessage();
    std::vector<uint8_t> CreateGetUserMessage(const std::string& username);
    std::vector<uint8_t> CreateChangeStatusMessage(EstadoUsuario status);
    std::vector<uint8_t> CreateSendMessageMessage(const std::string& dest, const std::string& message);
    std::vector<uint8_t> CreateGetHistoryMessage(const std::string& chat);

    void ProcessErrorMessage(const std::vector<uint8_t>& data);
    void ProcessListUsersMessage(const std::vector<uint8_t>& data);
    void ProcessUserInfoMessage(const std::vector<uint8_t>& data);
    void ProcessNewUserMessage(const std::vector<uint8_t>& data);
    void ProcessStatusChangeMessage(const std::vector<uint8_t>& data);
    void ProcessMessageMessage(const std::vector<uint8_t>& data);
    void ProcessHistoryMessage(const std::vector<uint8_t>& data);

    void UpdateContactListUI();
    void UpdateStatusDisplay();
    bool CanSendMessage() const;
    bool IsWebSocketConnected();
    bool ReiniciarConexion();
    bool VerificarConexion();
};

ChatFrame::ChatFrame(std::shared_ptr<websocket::stream<tcp::socket>> ws, const std::string& usuario)
    : wxFrame(nullptr, wxID_ANY, "Chat - " + usuario, wxDefaultPosition, wxSize(800, 600)), 
      ws_(ws), 
      usuario_(usuario),
      running_(true),
      currentStatus_(EstadoUsuario::ACTIVO),
      canSendMessages_(true),
      forceCanSend_(false) {

    contacts_.insert({"~", ContactInfo("Chat General", EstadoUsuario::ACTIVO)});
    contacts_.insert({usuario_, ContactInfo(usuario_, EstadoUsuario::ACTIVO)});
    
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* statusSizer = new wxBoxSizer(wxHORIZONTAL);
    statusSizer->Add(new wxStaticText(panel, wxID_ANY, "Estado:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    
    wxString statusChoices[] = {"Activo", "Ocupado", "Inactivo"};
    statusChoice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 3, statusChoices);
    statusChoice->SetSelection(0); 
    statusSizer->Add(statusChoice, 1, wxALL, 5);
    
    leftSizer->Add(statusSizer, 0, wxEXPAND);

    statusText = new wxStaticText(panel, wxID_ANY, "Estado actual: ACTIVO");
    statusText->SetForegroundColour(wxColour(0, 128, 0)); 
    leftSizer->Add(statusText, 0, wxALL, 5);

    leftSizer->Add(new wxStaticText(panel, wxID_ANY, "Contactos:"), 0, wxALL, 5);
    contactList = new wxListBox(panel, wxID_ANY);
    leftSizer->Add(contactList, 1, wxALL | wxEXPAND, 5);

    wxBoxSizer* contactButtonsSizer = new wxBoxSizer(wxHORIZONTAL);
    
    addContactButton = new wxButton(panel, wxID_ANY, "Agregar");
    contactButtonsSizer->Add(addContactButton, 1, wxALL, 5);

    checkUserInfoButton = new wxButton(panel, wxID_ANY, "Info");
    contactButtonsSizer->Add(checkUserInfoButton, 1, wxALL, 5);
    
    refreshUsersButton = new wxButton(panel, wxID_ANY, "Actualizar");
    contactButtonsSizer->Add(refreshUsersButton, 1, wxALL, 5);
    
    leftSizer->Add(contactButtonsSizer, 0, wxEXPAND);

    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

    chatTitle = new wxStaticText(panel, ID_CHAT_TITLE, "Chat con: [Seleccione un contacto]");
    rightSizer->Add(chatTitle, 0, wxALL, 5);

    chatBox = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
    rightSizer->Add(chatBox, 1, wxALL | wxEXPAND, 5);

    wxBoxSizer* inputSizer = new wxBoxSizer(wxHORIZONTAL);
    messageInput = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    inputSizer->Add(messageInput, 1, wxALL, 5);

    sendButton = new wxButton(panel, wxID_ANY, "Enviar");
    inputSizer->Add(sendButton, 0, wxALL, 5);
    
    rightSizer->Add(inputSizer, 0, wxEXPAND);

    mainSizer->Add(leftSizer, 1, wxEXPAND | wxALL, 10);
    mainSizer->Add(rightSizer, 2, wxEXPAND | wxALL, 10);

    sendButton->Bind(wxEVT_BUTTON, &ChatFrame::OnSend, this);
    messageInput->Bind(wxEVT_TEXT_ENTER, &ChatFrame::OnSend, this);
    addContactButton->Bind(wxEVT_BUTTON, &ChatFrame::OnAddContact, this);
    checkUserInfoButton->Bind(wxEVT_BUTTON, &ChatFrame::OnCheckUserInfo, this);
    refreshUsersButton->Bind(wxEVT_BUTTON, &ChatFrame::OnRefreshUsers, this);
    contactList->Bind(wxEVT_LISTBOX, &ChatFrame::OnSelectContact, this);
    statusChoice->Bind(wxEVT_CHOICE, &ChatFrame::OnChangeStatus, this);

    panel->SetSizer(mainSizer);
    mainSizer->Fit(this);

    StartReceivingMessages();
    RequestUserList();


    UpdateContactListUI();
    

    contactList->SetSelection(contactList->FindString("[A] Chat General"));
    chatPartner_ = "~";
    chatTitle->SetLabel("Chat con: Chat General");
}

ChatFrame::~ChatFrame() {
    running_ = false;
    try {
        ws_->close(websocket::close_code::normal);
    } catch (...) {

    }
}

void ChatFrame::RequestUserList() {
    try {
        std::vector<uint8_t> request = CreateListUsersMessage();
        ws_->write(net::buffer(request));
    } catch (const std::exception& e) {
        wxMessageBox("Error al solicitar la lista de usuarios: " + std::string(e.what()),
                    "Error", wxOK | wxICON_ERROR);
    }
}

void ChatFrame::LoadChatHistory() {
    if (chatPartner_.empty()) return;
    
    RequestChatHistory();
}

void ChatFrame::RequestChatHistory() {
    try {
        std::vector<uint8_t> request = CreateGetHistoryMessage(chatPartner_);
        ws_->write(net::buffer(request));
    } catch (const std::exception& e) {
        wxMessageBox("Error al solicitar historial: " + std::string(e.what()),
                    "Error", wxOK | wxICON_ERROR);
    }
}

bool ChatFrame::CanSendMessage() const {
    return currentStatus_ == EstadoUsuario::ACTIVO || currentStatus_ == EstadoUsuario::INACTIVO;
}

bool ChatFrame::IsWebSocketConnected() {
    if (!ws_) return false;
    
    try {
        return ws_->is_open() && ws_->next_layer().is_open();
    } catch (...) {
        return false;
    }
}

void ChatFrame::OnSend(wxCommandEvent&) {
    if (chatPartner_.empty()) {
        wxMessageBox("Seleccione un contacto primero", "Aviso", wxOK | wxICON_INFORMATION);
        return;
    }
    
    if (!canSendMessages_ && !forceCanSend_) {
        wxMessageBox("No puedes enviar mensajes en tu estado actual",
                    "Aviso", wxOK | wxICON_WARNING);
        return;
    }

    if (!VerificarConexion()) {
        return;
    }

    std::string message = messageInput->GetValue().ToStdString();
    if (message.empty()) return;

    try {
        std::vector<uint8_t> data = CreateSendMessageMessage(chatPartner_, message);
        if (data.empty()) return;

        ws_->write(net::buffer(data));
        messageInput->Clear();
    } catch (const std::exception& e) {
        if (ReiniciarConexion()) {
            try {
                std::vector<uint8_t> data = CreateSendMessageMessage(chatPartner_, message);
                ws_->write(net::buffer(data));
                messageInput->Clear();
            } catch (const std::exception& e2) {
                wxMessageBox("No se pudo enviar el mensaje: " + std::string(e2.what()),
                            "Error", wxOK | wxICON_ERROR);
            }
        } else {
            wxMessageBox("Error al enviar mensaje: " + std::string(e.what()),
                        "Error", wxOK | wxICON_ERROR);
        }
    }
}

void ChatFrame::StartReceivingMessages() {
    std::thread([this]() {
        try {
            while (running_) {
                beast::flat_buffer buffer;
                ws_->read(buffer);
                

                std::string data_str = beast::buffers_to_string(buffer.data());
                std::vector<uint8_t> message(data_str.begin(), data_str.end());
                
                if (!message.empty()) {
                    uint8_t code = message[0];
                    
                    switch (code) {
                        case SERVER_ERROR:
                            ProcessErrorMessage(message);
                            break;
                        case SERVER_LIST_USERS:
                            ProcessListUsersMessage(message);
                            break;
                        case SERVER_USER_INFO:
                            ProcessUserInfoMessage(message);
                            break;
                        case SERVER_NEW_USER:
                            ProcessNewUserMessage(message);
                            break;
                        case SERVER_STATUS_CHANGE:
                            ProcessStatusChangeMessage(message);
                            break;
                        case SERVER_MESSAGE:
                            ProcessMessageMessage(message);
                            break;
                        case SERVER_HISTORY:
                            ProcessHistoryMessage(message);
                            break;
                        default:
                            break;
                    }
                }
            }
        } catch (const beast::error_code& ec) {
            if (ec == websocket::error::closed) {
                wxGetApp().CallAfter([this]() {
                    wxMessageBox("Conexión cerrada por el servidor", "Aviso", wxOK | wxICON_INFORMATION);
                    Close();
                });
            } else {
                wxGetApp().CallAfter([this, ec]() {
                    wxMessageBox("Error en la conexión: " + ec.message(),
                                "Error", wxOK | wxICON_ERROR);
                    Close();
                });
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
            try {
                std::vector<uint8_t> request = CreateGetUserMessage(contactName);
                ws_->write(net::buffer(request));
            } catch (const std::exception& e) {
                wxMessageBox("Error al solicitar información de usuario: " + std::string(e.what()),
                           "Error", wxOK | wxICON_ERROR);
            }
        }
    }
}

void ChatFrame::OnSelectContact(wxCommandEvent& evt) {
    wxString selectedItem = contactList->GetString(evt.GetSelection());
    wxString contactName = selectedItem.AfterFirst(']').Trim(true).Trim(false);

    if (contactName == "Chat General") {
        chatPartner_ = "~";  
    } else {
        chatPartner_ = contactName.ToStdString();
    }

    wxString titleText = wxString("Chat con: ") + 
                      (chatPartner_ == "~" ? wxString("Chat General") : wxString(chatPartner_));
    chatTitle->SetLabel(titleText);

    chatBox->Clear();
    LoadChatHistory();
}

void ChatFrame::OnCheckUserInfo(wxCommandEvent&) {
    if (contactList->GetSelection() == wxNOT_FOUND) {
        wxMessageBox("Seleccione un usuario primero", "Aviso", wxOK | wxICON_INFORMATION);
        return;
    }

    if (!VerificarConexion()) {
        return; 
    }

    wxString selectedItem = contactList->GetString(contactList->GetSelection());
    wxString contactName = selectedItem.AfterFirst(']').Trim(true).Trim(false);
    std::string username = contactName.ToStdString();
    
    if (username == "~" || username == "Chat General") {
        wxMessageBox("No se puede obtener información del chat general", "Aviso", wxOK | wxICON_INFORMATION);
        return;
    }
    
    try {
        std::vector<uint8_t> request = CreateGetUserMessage(username);
        ws_->write(net::buffer(request));
    } catch (const std::exception& e) {
        if (ReiniciarConexion()) {
            try {
                std::vector<uint8_t> request = CreateGetUserMessage(username);
                ws_->write(net::buffer(request));
            } catch (const std::exception& e2) {
                wxMessageBox("No se pudo obtener información del usuario: " + std::string(e2.what()),
                          "Error", wxOK | wxICON_ERROR);
            }
        } else {
            wxMessageBox("Error al solicitar información de usuario: " + std::string(e.what()),
                       "Error", wxOK | wxICON_ERROR);
        }
    }
}

void ChatFrame::OnRefreshUsers(wxCommandEvent&) {
    RequestUserList();
}

void ChatFrame::UpdateStatusDisplay() {
    wxString statusString;
    wxColour statusColor;
    
    switch (currentStatus_) {
        case EstadoUsuario::ACTIVO:
            statusString = "ACTIVO";
            statusColor = wxColour(0, 128, 0);
            canSendMessages_ = true;
            break;
        case EstadoUsuario::OCUPADO:
            statusString = "OCUPADO";
            statusColor = wxColour(255, 0, 0);
            canSendMessages_ = false;
            break;
        case EstadoUsuario::INACTIVO:
            statusString = "INACTIVO";
            statusColor = wxColour(128, 128, 0);
            canSendMessages_ = true;
            break;
        case EstadoUsuario::DESCONECTADO:
            statusString = "DESCONECTADO";
            statusColor = wxColour(128, 128, 128);
            canSendMessages_ = false;
            break;
    }
    
    statusText->SetLabel("Estado actual: " + statusString);
    statusText->SetForegroundColour(statusColor);
    
    messageInput->Enable(canSendMessages_ || forceCanSend_);
    sendButton->Enable(canSendMessages_ || forceCanSend_);
    
    auto it = contacts_.find(usuario_);
    if (it != contacts_.end()) {
        it->second.estado = currentStatus_;
    }
    
    UpdateContactListUI();
}

void ChatFrame::OnChangeStatus(wxCommandEvent&) {
    int selection = statusChoice->GetSelection();
    EstadoUsuario newStatus;
    
    switch (selection) {
        case 0: 
            newStatus = EstadoUsuario::ACTIVO; 
            canSendMessages_ = true;
            break;
        case 1: 
            newStatus = EstadoUsuario::OCUPADO; 
            canSendMessages_ = false;
            break;
        case 2: 
            newStatus = EstadoUsuario::INACTIVO; 
            canSendMessages_ = true;
            break;
        default: 
            newStatus = EstadoUsuario::ACTIVO; 
            canSendMessages_ = true;
            break;
    }
    
    try {
        currentStatus_ = newStatus;
        UpdateStatusDisplay();
        
        auto it = contacts_.find(usuario_);
        if (it != contacts_.end()) {
            it->second.estado = newStatus;
        }
        UpdateContactListUI();
        
        std::vector<uint8_t> message = CreateChangeStatusMessage(newStatus);
        ws_->write(net::buffer(message));
    } catch (const std::exception& e) {
        std::cerr << "Error al enviar cambio de estado: " << e.what() << std::endl;
        wxMessageBox("Error al cambiar estado: " + std::string(e.what()), 
                   "Error", wxOK | wxICON_ERROR);
    }
}

bool ChatFrame::ReiniciarConexion() {
    try {

        ws_->close(websocket::close_code::normal);

        net::io_context ioc;
        tcp::resolver resolver(ioc);

        std::string ip = ws_->next_layer().remote_endpoint().address().to_string();
        unsigned short puerto = ws_->next_layer().remote_endpoint().port();
        
        auto const results = resolver.resolve(ip, std::to_string(puerto));
        
        tcp::socket socket(ioc);
        net::connect(socket, results);
        
        auto new_ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));
        new_ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        
        std::string host = ip;
        std::string target = "/?name=" + usuario_;
        
        new_ws->handshake(host, target);

        ws_ = new_ws;
        RequestUserList();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error al reiniciar conexión: " << e.what() << std::endl;
        return false;
    }
}

bool ChatFrame::VerificarConexion() {
    if (!IsWebSocketConnected()) {
        bool reconectado = ReiniciarConexion();
        if (reconectado) {
            wxMessageBox("La conexión se ha restablecido con éxito.", 
                       "Reconexión", wxOK | wxICON_INFORMATION);
            return true;
        } else {
            wxMessageBox("No se pudo restablecer la conexión con el servidor.", 
                       "Error de Conexión", wxOK | wxICON_ERROR);
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> ChatFrame::CreateListUsersMessage() {
    return {CLIENT_LIST_USERS};
}

std::vector<uint8_t> ChatFrame::CreateGetUserMessage(const std::string& username) {
    std::vector<uint8_t> message = {CLIENT_GET_USER, static_cast<uint8_t>(username.size())};
    message.insert(message.end(), username.begin(), username.end());
    return message;
}

std::vector<uint8_t> ChatFrame::CreateChangeStatusMessage(EstadoUsuario status) {
    std::vector<uint8_t> message = {
        CLIENT_CHANGE_STATUS, 
        static_cast<uint8_t>(usuario_.size())
    };
    message.insert(message.end(), usuario_.begin(), usuario_.end());
    message.push_back(static_cast<uint8_t>(status));
    return message;
}

std::vector<uint8_t> ChatFrame::CreateSendMessageMessage(const std::string& dest, const std::string& message) {
    if (message.size() > 255) {
        wxMessageBox("El mensaje es demasiado largo (máximo 255 caracteres)", 
                    "Aviso", wxOK | wxICON_WARNING);
        return {};
    }
    
    try {
        std::vector<uint8_t> data = {
            CLIENT_SEND_MESSAGE, 
            static_cast<uint8_t>(dest.size())
        };
        data.insert(data.end(), dest.begin(), dest.end());
        data.push_back(static_cast<uint8_t>(message.size()));
        data.insert(data.end(), message.begin(), message.end());
        return data;
    } catch (const std::exception& e) {
        wxMessageBox("Error al crear mensaje: " + std::string(e.what()), 
                   "Error", wxOK | wxICON_ERROR);
        return {};
    }
}

std::vector<uint8_t> ChatFrame::CreateGetHistoryMessage(const std::string& chat) {
    std::vector<uint8_t> message = {CLIENT_GET_HISTORY, static_cast<uint8_t>(chat.size())};
    message.insert(message.end(), chat.begin(), chat.end());
    return message;
}


void ChatFrame::ProcessErrorMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    ErrorCode errorCode = static_cast<ErrorCode>(data[1]);
    wxString errorMessage;
    
    switch (errorCode) {
        case ERROR_USER_NOT_FOUND:
            errorMessage = "El usuario solicitado no existe";
            break;
        case ERROR_INVALID_STATUS:
            errorMessage = "Estado de usuario inválido";
            break;
        case ERROR_EMPTY_MESSAGE:
            errorMessage = "No se puede enviar un mensaje vacío";
            break;
        case ERROR_DISCONNECTED_USER:
            errorMessage = "No se puede enviar mensaje a un usuario desconectado";
            break;
        default:
            errorMessage = "Error desconocido";
            break;
    }
    
    wxGetApp().CallAfter([errorMessage]() {
        wxMessageBox(errorMessage, "Error", wxOK | wxICON_ERROR);
    });
}

void ChatFrame::ProcessListUsersMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    uint8_t numUsers = data[1];
    size_t offset = 2;

    ContactInfo chatGeneral = contacts_["~"];
    EstadoUsuario currentUserStatus = EstadoUsuario::ACTIVO;
    auto it = contacts_.find(usuario_);
    if (it != contacts_.end()) {
        currentUserStatus = it->second.estado;
    }
    
    contacts_.clear();
    contacts_["~"] = chatGeneral;

    contacts_[usuario_] = ContactInfo(usuario_, currentUserStatus);
    
    for (uint8_t i = 0; i < numUsers; i++) {
        if (offset >= data.size()) break;
        
        uint8_t userLen = data[offset++];
        if (offset + userLen > data.size()) break;
        
        std::string username(data.begin() + offset, data.begin() + offset + userLen);
        offset += userLen;
        
        if (offset >= data.size()) break;
        
        EstadoUsuario status = static_cast<EstadoUsuario>(data[offset++]);

        if (username == usuario_) {
            currentStatus_ = status;
        }

        contacts_.emplace(username, ContactInfo(username, status));
    }
    
    wxGetApp().CallAfter([this]() {
        UpdateStatusDisplay();
        UpdateContactListUI();
    });
}

void ChatFrame::ProcessUserInfoMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    uint8_t userLen = data[1];
    if (2 + userLen > data.size()) return;
    
    std::string username(data.begin() + 2, data.begin() + 2 + userLen);
    
    if (2 + userLen + 1 > data.size()) return;
    
    EstadoUsuario status = static_cast<EstadoUsuario>(data[2 + userLen]);
    
    std::string statusStr;
    switch (status) {
        case EstadoUsuario::ACTIVO: statusStr = "Activo"; break;
        case EstadoUsuario::OCUPADO: statusStr = "Ocupado"; break;
        case EstadoUsuario::INACTIVO: statusStr = "Inactivo"; break;
        case EstadoUsuario::DESCONECTADO: statusStr = "Desconectado"; break;
    }
    
    wxGetApp().CallAfter([username, statusStr]() {
        wxString info = wxString::Format(
            "Información del usuario %s:\n"
            "Estado: %s",
            username, statusStr
        );
        
        wxMessageBox(info, "Información de Usuario", wxOK | wxICON_INFORMATION);
    });
}

void ChatFrame::ProcessNewUserMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    uint8_t userLen = data[1];
    if (2 + userLen > data.size()) return;
    
    std::string username(data.begin() + 2, data.begin() + 2 + userLen);
    
    if (2 + userLen + 1 > data.size()) return;
    
    EstadoUsuario status = static_cast<EstadoUsuario>(data[2 + userLen]);
    

    contacts_.emplace(username, ContactInfo(username, status));
    
    wxGetApp().CallAfter([this]() {
        UpdateContactListUI();
    });
}

void ChatFrame::ProcessStatusChangeMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    uint8_t userLen = data[1];
    if (2 + userLen > data.size()) return;
    
    std::string username(data.begin() + 2, data.begin() + 2 + userLen);
    
    if (2 + userLen + 1 > data.size()) return;
    
    EstadoUsuario status = static_cast<EstadoUsuario>(data[2 + userLen]);
    
    auto it = contacts_.find(username);
    if (it != contacts_.end()) {
        it->second.estado = status;
    } else {
        contacts_.emplace(username, ContactInfo(username, status));
    }

    if (username == usuario_) {
        currentStatus_ = status;
        
        wxGetApp().CallAfter([this, status]() {
            switch (status) {
                case EstadoUsuario::ACTIVO:
                    statusChoice->SetSelection(0);
                    canSendMessages_ = true;
                    break;
                case EstadoUsuario::OCUPADO:
                    statusChoice->SetSelection(1);
                    canSendMessages_ = false;
                    break;
                case EstadoUsuario::INACTIVO:
                    statusChoice->SetSelection(2);
                    canSendMessages_ = true;
                    break;
                default:
                    break;
            }
            
            UpdateStatusDisplay();
        });
    } else {
        wxGetApp().CallAfter([this]() {
            UpdateContactListUI();
        });
    }
}

void ChatFrame::ProcessMessageMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    uint8_t originLen = data[1];
    if (2 + originLen > data.size()) return;
    
    std::string origin(data.begin() + 2, data.begin() + 2 + originLen);
    
    if (2 + originLen + 1 > data.size()) return;
    
    uint8_t msgLen = data[2 + originLen];
    if (3 + originLen + msgLen > data.size()) return;
    
    std::string message(data.begin() + 3 + originLen, data.begin() + 3 + originLen + msgLen);

    std::string formatted = origin + ": " + message;
    
    {
        std::lock_guard<std::mutex> lock(chatHistoryMutex_);

        std::string chatKey;
        if (origin == usuario_) {
            chatKey = chatPartner_;
        } else {
            chatKey = (chatPartner_ == "~") ? "~" : origin;
        }
        
        chatHistory_[chatKey].push_back(formatted);
    }

    if (chatPartner_ == "~" || origin == chatPartner_ || origin == usuario_) {
        wxGetApp().CallAfter([this, formatted]() {
            chatBox->AppendText(formatted + "\n");
        });
    }
}

void ChatFrame::ProcessHistoryMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 2) return;
    
    uint8_t numMessages = data[1];
    size_t offset = 2;
    
    std::vector<std::string> messages;
    
    for (uint8_t i = 0; i < numMessages; i++) {
        if (offset >= data.size()) break;
        
        uint8_t userLen = data[offset++];
        if (offset + userLen > data.size()) break;
        
        std::string username(data.begin() + offset, data.begin() + offset + userLen);
        offset += userLen;
        
        if (offset >= data.size()) break;
        
        uint8_t msgLen = data[offset++];
        if (offset + msgLen > data.size()) break;
        
        std::string message(data.begin() + offset, data.begin() + offset + msgLen);
        offset += msgLen;

        std::string formatted = username + ": " + message;
        messages.push_back(formatted);
    }
    
    {
        std::lock_guard<std::mutex> lock(chatHistoryMutex_);
        chatHistory_[chatPartner_] = messages;
    }
    
    wxGetApp().CallAfter([this, messages]() {
        chatBox->Clear();
        for (const auto& msg : messages) {
            chatBox->AppendText(msg + "\n");
        }
    });
}

void ChatFrame::UpdateContactListUI() {
    int currentSelection = contactList->GetSelection();
    wxString currentItem = (currentSelection != wxNOT_FOUND) ? 
                         contactList->GetString(currentSelection) : wxString();

    contactList->Clear();
    
    for (const auto& [name, info] : contacts_) {
        contactList->Append(info.FormatName());
    }

    if (!currentItem.IsEmpty()) {
        int pos = contactList->FindString(currentItem);
        if (pos != wxNOT_FOUND) {
            contactList->SetSelection(pos);
        } else {
            wxString contactName = currentItem.AfterFirst(']').Trim(true).Trim(false);
            for (unsigned int i = 0; i < contactList->GetCount(); i++) {
                if (contactList->GetString(i).AfterFirst(']').Trim(true).Trim(false) == contactName) {
                    contactList->SetSelection(i);
                    break;
                }
            }
        }
    }
}

class MyFrame : public wxFrame {
public:
    MyFrame() : wxFrame(nullptr, wxID_ANY, "Cliente WebSocket", wxDefaultPosition, wxSize(400, 250)) {
        wxPanel* panel = new wxPanel(this);
        wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* userSizer = new wxBoxSizer(wxHORIZONTAL);
        userSizer->Add(new wxStaticText(panel, wxID_ANY, "Nombre de usuario:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
        nombreInput = new wxTextCtrl(panel, wxID_ANY);
        userSizer->Add(nombreInput, 1, wxALL, 10);
        mainSizer->Add(userSizer, 0, wxEXPAND);

        wxBoxSizer* ipSizer = new wxBoxSizer(wxHORIZONTAL);
        ipSizer->Add(new wxStaticText(panel, wxID_ANY, "IP del servidor:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
        ipInput = new wxTextCtrl(panel, wxID_ANY, "3.13.27.172");
        ipSizer->Add(ipInput, 1, wxALL, 10);
        mainSizer->Add(ipSizer, 0, wxEXPAND);

        wxBoxSizer* portSizer = new wxBoxSizer(wxHORIZONTAL);
        portSizer->Add(new wxStaticText(panel, wxID_ANY, "Puerto:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
        puertoInput = new wxTextCtrl(panel, wxID_ANY, "3000");
        portSizer->Add(puertoInput, 1, wxALL, 10);
        mainSizer->Add(portSizer, 0, wxEXPAND);

        wxButton* conectarButton = new wxButton(panel, wxID_ANY, "Conectar");
        mainSizer->Add(conectarButton, 0, wxALL | wxCENTER, 10);

        statusLabel = new wxStaticText(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
        statusLabel->SetForegroundColour(wxColour(255, 0, 0));
        mainSizer->Add(statusLabel, 0, wxALL | wxEXPAND, 10);

        conectarButton->Bind(wxEVT_BUTTON, &MyFrame::OnConectar, this);
        panel->SetSizer(mainSizer);
    }

private:
    wxTextCtrl* nombreInput;
    wxTextCtrl* ipInput;
    wxTextCtrl* puertoInput;
    wxStaticText* statusLabel;

void OnConectar(wxCommandEvent&) {
    std::string usuario = nombreInput->GetValue().ToStdString();
    std::string ip = ipInput->GetValue().ToStdString();
    std::string puerto = puertoInput->GetValue().ToStdString();
    
    if (usuario.empty()) {
        statusLabel->SetLabel("Error: El nombre de usuario no puede estar vacío");
        return;
    }
    
    if (usuario == "~") {
        statusLabel->SetLabel("Error: El nombre '~' está reservado para el chat general");
        return;
    }
    
    if (ip.empty()) {
        statusLabel->SetLabel("Error: La IP del servidor no puede estar vacía");
        return;
    }
    
    if (puerto.empty()) {
        statusLabel->SetLabel("Error: El puerto no puede estar vacío");
        return;
    }
    
    statusLabel->SetLabel("Conectando...");

    std::thread([this, usuario, ip, puerto]() {
        try {
            std::cout << "Iniciando conexión a " << ip << ":" << puerto << std::endl;

            net::io_context ioc;
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(ip, puerto);
            
            std::cout << "Dirección resuelta, conectando socket TCP..." << std::endl;
    
            tcp::socket socket(ioc);
            net::connect(socket, results.begin(), results.end());
            
            std::cout << "Socket TCP conectado, creando stream WebSocket..." << std::endl;

            auto ws = std::make_shared<websocket::stream<tcp::socket>>(std::move(socket));

            ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

            std::string host = ip;
            std::string target = "/?name=" + usuario;
            
            std::cout << "Iniciando handshake WebSocket con host=" << host 
                     << " y target=" << target << std::endl;
    

            ws->handshake(host, target);
            std::cout << "Handshake WebSocket exitoso!" << std::endl;
    

            wxGetApp().CallAfter([this, ws, usuario]() {
                ChatFrame* chatFrame = new ChatFrame(ws, usuario);
                chatFrame->Show(true);
                Close();
            });
        } 
        catch (const beast::error_code& ec) {
            wxGetApp().CallAfter([this, ec]() {
                std::string errorMsg = "Error de conexión: " + ec.message();
                statusLabel->SetLabel("Error: " + errorMsg);
                std::cerr << errorMsg << std::endl;
            });
        }
        catch (const std::exception& e) {
            wxGetApp().CallAfter([this, e]() {
                std::string errorMsg = e.what();
                statusLabel->SetLabel("Error: " + errorMsg);
                std::cerr << "Excepción: " << errorMsg << std::endl;
            });
        }
        catch (...) {
            wxGetApp().CallAfter([this]() {
                statusLabel->SetLabel("Error: Excepción desconocida durante la conexión");
                std::cerr << "Error desconocido durante la conexión" << std::endl;
            });
        }
    }).detach();
}
};

bool MyApp::OnInit() {
    MyFrame* frame = new MyFrame();
    frame->Show(true);
    return true;
}