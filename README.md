# HTTP Server in C

Servidor HTTP/1.1 desarrollado desde cero en C para Linux.

El objetivo del proyecto fue comprender en profundidad cómo funcionan los servidores web a bajo nivel, implementando directamente el manejo de sockets, concurrencia y cacheo de archivos sin utilizar frameworks externos.

## Características

- Implementación de HTTP/1.1
- Sockets POSIX
- Soporte IPv4 e IPv6 (dual-stack)
- Arquitectura multiproceso con un worker por núcleo utilizando `fork()`
- Multiplexación de eventos mediante `epoll`
- Cache en memoria para archivos estáticos
- Invalidación automática del cache cuando los archivos cambian
- Soporte para conexiones keep-alive
- Manejo de timeouts de clientes
- Protección contra ataques de path traversal mediante validación de rutas

## Arquitectura

El servidor crea un proceso worker por cada núcleo disponible del sistema.

Cada worker:

1. Comparte el socket de escucha.
2. Acepta nuevas conexiones.
3. Gestiona miles de conexiones concurrentes mediante `epoll`.
4. Sirve contenido estático desde memoria cuando es posible.

## Tecnologías utilizadas

- C
- Linux
- POSIX Sockets
- epoll
- fork()
- señales UNIX

## Conceptos aplicados

- Programación de sistemas
- Sistemas operativos
- Redes TCP/IP
- Concurrencia
- Gestión de memoria
- Programación orientada a eventos
- Optimización de rendimiento

## Ejecución

```bash
make
./server
```

Por defecto el servidor escucha en el puerto 4080.

## Aprendizajes

Este proyecto permitió explorar cómo funcionan internamente los servidores web modernos, incluyendo modelos de concurrencia basados en procesos, manejo eficiente de eventos y estrategias de cacheo para reducir accesos al disco.
