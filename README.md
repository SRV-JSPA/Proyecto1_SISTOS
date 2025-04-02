# Proyecto #1 - Chat

Este proyecto es una aplicación de chat en tiempo real implementada en C++ utilizando **WebSockets**, con un servidor basado en **Boost.Beast** y un cliente con interfaz gráfica en **wxWidgets**. Fue desarrollado y probado en **macOS**.

---

## Requisitos para correr el proyecto

Antes de compilar o ejecutar este proyecto, verificar de tener instalado lo siguiente:

-> Un compilador compatible con **C++17**
-> **Boost 1.87.0** (incluyendo Boost.Asio y Boost.Beast)
-> **wxWidgets** (para la GUI del cliente)

### Instalación de dependencias en macOS

Usando [Homebrew](https://brew.sh/):

```bash
brew install boost wxwidgets
```

---

## Compilación

### Cliente

```bash
g++ cliente.cpp -o cliente `wx-config --cxxflags --libs` \
    -I/opt/homebrew/Cellar/boost/1.87.0/include \
    -L/opt/homebrew/Cellar/boost/1.87.0/lib \
    -lboost_system -lpthread -std=c++17
```

### Servidor

```bash
g++ servidor.cpp -o servidor \
    -I/opt/homebrew/Cellar/boost/1.87.0/include \
    -L/opt/homebrew/Cellar/boost/1.87.0/lib \
    -lboost_system -lpthread -std=c++17
```

> **Nota**: Las rutas de las librerías (`-I` y `-L`) pueden variar dependiendo del sistema operativo. Deben de ajustarlas a su sistema operativo.

---

## Ejecución

### Servidor

Ejecutá el servidor especificando un puerto (por ejemplo, `3000`):

```bash
./servidor 3000
```

Esto inicia el servidor y lo deja escuchando conexiones WebSocket en ese puerto.

### Cliente

 El cliente se ejecuta con:

```bash
./cliente
```

Se abrirá la interfaz donde se debe de ingresar lo siguiente:

1. EL **nombre de usuario**
2. La **IP del servidor** (`127.0.0.1` si está en la máquina, si se va a conectar a otro servidor puede variar)
3. El **puerto** (el mismo que se utiliza en el servidor, ej. `3000`, si se va a conectar a otro servidor puede variar)
4. Dar click en el botón de **Conectar**

---

## Características

- Comunicación en tiempo real
- Interfaz gráfica 
- Envío de mensajes privados o al chat general
- Lista de contactos con estados:
  - **Activo**
  - **Ocupado**
  - **Inactivo**
  - **Desconectado**
- Historial de conversaciones
- Actualización automática del estado por inactividad
- Logging en el servidor (`chat_server.log`)

---

## Restricciones y Consideraciones

- Si un usuario no realiza actividad, pasa automáticamente a **INACTIVO**
- No se pueden mandar mensajes a usuarios **desconectados** u **ocupados**

---

## 🧪 Sistema Operativo y Portabilidad

- Este proyecto fue hecho y probado en **macOS**
- Si usás **Linux**:
  - Verificá que tengas `wx-config` en el `PATH`
  - Cambiá rutas de librerías si no usás Homebrew
- Si usás **Windows**:
  - Usá **MSYS2**, **WSL**, o **Visual Studio con CMake**
  - Configurá bien las rutas a Boost y wxWidgets (es un quilombo, avisame si necesitás ayuda)


