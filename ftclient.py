#!/usr/bin/env python3

'''
Program Name: ftclient.py
Programmer Name: Jordan Hamilton
Course Name: CS 372 - Intro to Computer Networks
Last Modified: 3/9/2020
Description: This application parses a command to either list directory contents on a server or get a file
located in that directory, then passes the client's fully qualified domain name and a port so the server can
establish a data connection on the specified port to send the requested data to the client. If a valid file
was requested and a file with the same name does not exist already in the same directory on the client, then
the same file is created in the client's current working directory. If the contents of the server's directory
are requested by the client, that directory's contents are printed to the screen.
'''

from socket import socket, AF_INET, SHUT_RDWR, SOCK_STREAM, getfqdn
import argparse
from os import listdir, path


def main():
    # A unique string indicating the end of the message being sent from the client.
    end_of_message = '||'
    # Verbs to either get or list files on the server
    get_cmd = '-g'
    list_cmd = '-l'

    # Read and validate arguments passed to the program
    args = parse_args()

    # Get the hostname, port number and command entered on the command line
    server_host = args.server_host
    server_port = args.server_port
    cmd = list_cmd if args.l is not None else '{} {}'.format(get_cmd, args.g[0])

    try:
        # Set the data port to the value specified after either the -l command, or the -g command and filename
        data_port = args.l[0] if args.l is not None else int(args.g[1])

        # Display an error and exit if either the data or control port is outside of a valid port range
        if not 1 <= data_port <= 65535 or not 1 <= server_port <= 65535:
            raise ValueError
    except ValueError:
        print('Please provide valid port numbers and try again.')
        raise SystemExit(2)

    # Send the command selected by the user to the server, then listen and retrieve the data sent back
    send_receive_data(cmd, end_of_message, data_port, get_cmd, list_cmd, args)

    # Exit the process.
    raise SystemExit(0)


# This function takes the port to use for the data connection, as well as a string that indicates the end of message
# being sent by the server and the command line arguments that were parsed by the client. The client then enables a
# socket and listens on it for the response from the server to either the list or get command, displaying the
# appropriate messages on the client and receiving data from the socket until the end of the message is found.
# Finally, the connection is closed and either a new file is made when getting a file, or the directory contents on
# the server are displayed.
def get_data(data_port, end_of_message, args):

    with socket(AF_INET, SOCK_STREAM) as data_sock:
        # Attempt to bind the socket to the address and accept incoming connections.
        # Credit: https://docs.python.org/3/library/socket.html#example
        data_sock.bind(('', int(data_port)))
        data_sock.listen(1)
        conn, addr = data_sock.accept()

        with conn:
            # Print formatted text to the console to display what data we expect to receive from the server.
            if args.l is None:
                print( 'Receiving "{}" from {}:{}'.format(args.g[0], args.server_host, data_port) )
            else:
                print( 'Receiving directory structure from {}:{}'.format(args.server_host, data_port) )

            # Make a variable to store all data received from the socket, as well as a temporary variable to
            # store a fragment of what was sent. Receive data and store it in the temp variable.
            complete_data = b''
            data = conn.recv(1024)
            complete_data += data

            # If we don't detect the special characters to indicate the end of the message, continue looping and
            # appending more data until we find the end of the message.
            while end_of_message.encode() not in data:
                data = conn.recv(1024)
                if not data: break
                complete_data += data

            # Decode the complete data and remove the end of message indicator.
            complete_data = complete_data.decode().rstrip('||')

            # Shut down the socket for the established connection to the server and the port we're listening on.
            shutdown_conn(conn, data_sock)


        if args.g is not None:
            # Create a text file based on the data we received if we were getting a file.
            make_file(complete_data, args.g[0])
        else:
            # Display the received data without making a file if we only requested the directory contents.
            print(complete_data, end='')


# This function takes a string with the contents of a file and a name to provide to a file once those contents are
# written completely. The client's current working directory is read for all existing files first to ensure a file
# with the same name does not yet exist, or cancels the file creation if a duplicate is found. Otherwise, the file
# is written with the provided file name and the file is closed.
def make_file(contents, fileName):
    # Get all files in the current directory. If one has the same name as the current file, do not overwrite
    # it and display an error instead
    write = True

    # Get all the files in the current directory.
    # Credit: https://stackoverflow.com/questions/11968976/list-files-only-in-the-current-directory
    files = [f for f in listdir('.') if path.isfile(f)]

    # If a file in the files list has the same name as the file we're trying to create,
    # set the write flag to false, display an error message and don't check any more file names
    for f in files:
        if f == fileName:
            print('A file with the name {} already exists.\n'
                  'Please move it out of the current directory if you\'d like to save the requested file.'
                  .format(fileName))
            write = False
            break

    # If we checked all files in the current directory and couldn't find a duplicate name,
    # write the received data to a file with the name that was passed in command line arguments.
    if write:
        file = open(fileName, 'w')
        file.write(contents)
        file.close()
        print("File transfer complete.")


# This function specifies the arguments that must be specified at the command line when executing the client,
# then returns an object with the provided arguments to be used throughout the program.
def parse_args():
    parser = argparse.ArgumentParser()

    # Require arguments for both the server's hostname and port number.
    parser.add_argument('server_host',
                        help='The server hostname you\'d like to try to connect to',
                        type=str)
    parser.add_argument('server_port',
                        help='The server port number you\'d like this client to connect to',
                        type=int)

    # Require either the -l or -g command, accepting 1 argument (the data port) for -l, or 2 arguments
    # (the filename and the data port) for -g.
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('-l',
                       help='List files in the server\'s current working directory',
                       nargs=1,
                       type=int)
    group.add_argument('-g',
                       help='Get a file in the server\'s current working directory',
                       nargs=2)

    return parser.parse_args()


# This function takes a command to send to the server, a string indicating the end of a message sent to or
# from the client, an integer representing the port to use when establishing a separate data connection to
# the server, strings representing the get and list commands, and an object with the arguments passed to
# the client at the command line. A socket is created and the client connects to the server via the control
# port and sends the command parsed in main (either -g FILENAME or -l) so the server can parse the appropriate
# action to take and store and find the file name specified if needed before attempting to make a data connection.
# The client waits for the server to respond with the same command that was sent without arguments, (either -l or -g)
# to verify the data can be received and parsed correctly by the client. If an incorrect command is received, the
# client displays an error message and doesn't create a data connection. Otherwise, the handshake is complete and
# the client sends the server its fully qualified domain name and a port that it will listen on to make a data
# connection to receive the requested information using the get_data function.
def send_receive_data(cmd, end_of_message, data_port, get_cmd, list_cmd, args):
    # Create a socket and attempt to connect to the server.
    # Credit: https://docs.python.org/3/library/socket.html#example
    with socket(AF_INET, SOCK_STREAM) as control_sock:

        # Send the command requested by the user (either -g or -l) to the server.
        message = cmd + end_of_message

        control_sock.connect((args.server_host, args.server_port))
        control_sock.sendall(message.encode())

        # Wait for the server to respond with the same command we sent to ensure our message was
        # acknowledged correctly
        response = control_sock.recv(1024)

        if response.decode() == message.split()[0].rstrip(end_of_message) \
                or response.decode() == message.split()[0].rstrip(end_of_message):
            # Send the FQDN and port back to the server so it can send data via the data connection
            message = '{} {}{}'.format(getfqdn(), data_port, end_of_message)
            control_sock.sendall(message.encode())

            # Retrieve the data sent from the server if the command was valid
            get_data(data_port, end_of_message, args)
        else:
            # Print the error message the server sent (since our command wasn't acknowledged with the same command)
            print(response.decode())
            shutdown_conn(control_sock)
            raise SystemExit(1)

        shutdown_conn(control_sock)


# This function attempts to close the control or data connection to the server and stops listening
# for incoming connections.
def shutdown_conn(conn, *args):
    # Close the client connection.
    conn.shutdown(SHUT_RDWR)
    conn.close()

    # Stop listening on the port the data connection is bound to.
    if len(args) == 1:
        sock = args[0]
        sock.shutdown(SHUT_RDWR)
        sock.close()


if __name__ == "__main__":
    main()
