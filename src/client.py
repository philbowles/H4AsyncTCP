import socket 
import sys 
 
import argparse 
 
host = '192.168.1.200' 
 
def echo_client(port): 
    """ A simple echo client """ 
    # Create a TCP/IP socket 
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM) 
    # Connect the socket to the server 
    server_address = (host, port) 
    print ("Connecting to %s port %s" % server_address) 
    sock.connect(server_address) 
     
    # Send data 
    try: 
        # Send data 
        message = "H4EchoServer using H4AsyncTCP returns this message" 
        print ("Sending %s" % message) 
        sock.sendall(message.encode('utf-8')) 
        # Look for the response 
        amount_received = 0 
        amount_expected = len(message) 
        while amount_received < amount_expected: 
            data = sock.recv(128) 
            amount_received += len(data) 
            print ("Received: %s" % data) 
    except socket.error as e: 
        print ("Socket error: %s" %str(e)) 
    except Exception as e: 
        print ("Other exception: %s" %str(e)) 
    finally: 
        print ("Closing connection to the server") 
        sock.close() 
     
if __name__ == '__main__': 
    echo_client(8080) 