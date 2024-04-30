all: QRServer QRClient

QRServer: TCPEchoServer.c
	gcc TCPEchoServer.c -o QRServer

QRClient: TCPEchoClient.c
	gcc TCPEchoClient.c -o QRClient

clean:
	rm -f QRClient QRServer