import asyncore
import socket

ANSWER = 'HTTP/1.0 200 OK\r\n' + \
    'Content-Type: text/html; charset=utf-8\r\n' + \
    'Content-Length: 0\r\n' + \
    'Connection: close\r\n\r\n'

class EchoHandler(asyncore.dispatcher_with_send):

    def handle_read(self):
        data = self.recv(16384)
        print("RX ",len(data))
        print(data)

        if(data):
            self.send(data)

class EchoServer(asyncore.dispatcher):

    def __init__(self, host, port):
        asyncore.dispatcher.__init__(self)
        self.create_socket(socket.AF_INET, socket.SOCK_STREAM)
        self.set_reuse_addr()
        self.bind((host, port))
        self.listen(5)

    def handle_accept(self):
        pair = self.accept()
        if pair is not None:
            sock, addr = pair
            print ('Incoming connection from %s' % repr(addr))
            handler = EchoHandler(sock)

server = EchoServer('0.0.0.0', 8080)
asyncore.loop()