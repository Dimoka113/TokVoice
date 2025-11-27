import socket
import threading

RTP_PORT = 3030
BUFFER_SIZE = 2048

class RTPServer:
    def __init__(self, host='0.0.0.0', port=RTP_PORT):
        self.host = host
        self.port = port
        self.clients = [] 
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.server_socket.bind((self.host, self.port))
        print(f"RTP Server started on {self.host}:{self.port}")

    def handle_incoming_packets(self):
        while True:
            try:
                data, addr = self.server_socket.recvfrom(BUFFER_SIZE)
                print(f"Received packet from {addr}")
                
                if addr not in self.clients:
                    self.clients.append(addr)
                    print(f"New client added: {addr}")

                self.forward_packet(data, addr)
            except Exception as e:
                print(f"Error: {e}")

    def forward_packet(self, data, sender_addr):
        for client in self.clients:
            if client != sender_addr: 
                try:
                    self.server_socket.sendto(data, client)
                    print(f"Forwarded packet to {client}")
                except Exception as e:
                    print(f"Error forwarding to {client}: {e}")

    def start(self):
        thread = threading.Thread(target=self.handle_incoming_packets)
        thread.daemon = True
        thread.start()
        print("RTP Server is running and ready to handle packets.")

if __name__ == "__main__":
    server = RTPServer()
    server.start()

    # Keep the server running
    try:
        while True:
            pass
    except KeyboardInterrupt:
        print("\nServer shutting down.")
