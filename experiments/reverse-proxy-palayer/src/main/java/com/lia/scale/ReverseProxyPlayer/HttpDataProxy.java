package com.lia.scale.ReverseProxyPlayer;

import java.io.*;
import java.net.Socket;

public class HttpDataProxy extends Thread  {
    private Socket sClient;
    private String SERVER_URL = "";
    private int SERVER_PORT = 0;

    HttpDataProxy(Socket sClient, String ServerUrl, int ServerPort) {
        this.SERVER_URL = ServerUrl;
        this.SERVER_PORT = ServerPort;
        this.sClient = sClient;
        this.start();
    }

    @Override
    public void run() {
        try {
            final byte[] request = new byte[1024];
            byte[] reply = new byte[4096];
            final InputStream inFromClient = sClient.getInputStream();
            final OutputStream outToClient = sClient.getOutputStream();
            Socket client = null, server = null;
            // connects a socket to the server
            long threadId = Thread.currentThread().getId();
            System.out.printf("Message:%d: NEW proxy, tread id - %d\n", threadId, threadId);
            try {
                server = new Socket(SERVER_URL, SERVER_PORT);
            } catch (IOException e) {
                PrintWriter out = new PrintWriter(new OutputStreamWriter(outToClient));
                out.flush();
                throw new RuntimeException(e);
            }
            // a new thread to manage streams from server to client (DOWNLOAD)
            final InputStream inFromServer = server.getInputStream();
            final OutputStream outToServer = server.getOutputStream();

            new Thread() {
                public void run() {
                    int bytes_read;
                    try {
                        while ((bytes_read = inFromClient.read(request)) != -1){
                            String requestString = new String(request,0, bytes_read);
                            System.out.printf("REQUEST:%d: %s\n", threadId, requestString);
                            // TODO: respond to server/delay/skip?

                            outToServer.write(request, 0, bytes_read);
                            outToServer.flush();
                        }
                    } catch (IOException e) {
                        //System.out.println("Message: connection closed nothing to read from client.");
                        //e.printStackTrace();
                    }
                    try {
                        outToServer.close();
                    } catch (IOException e) {
                        e.printStackTrace();
                    }
                }
            }.start();

            try {
                int bytes_read;
                while ((bytes_read = inFromServer.read(reply)) != -1) {

                    String requestString = new String(reply,0, bytes_read);
                    System.out.printf("RESPONSE:%d: %s\n", threadId, requestString);
                    // TODO: respond to server/delay/skip?

                    outToClient.write(reply, 0, bytes_read);
                    outToClient.flush();
                }
            } catch (IOException e) {
                //System.out.println("Message: connection closed, nothing to read.");
                //e.printStackTrace();
            } finally {
                try {
                    if (server != null)
                        server.close();
                    if (client != null)
                        client.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
                System.out.printf("Message:%d: CLOSE proxy, tread id - %d\n", threadId, threadId);
            }
            outToClient.close();
            sClient.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}
