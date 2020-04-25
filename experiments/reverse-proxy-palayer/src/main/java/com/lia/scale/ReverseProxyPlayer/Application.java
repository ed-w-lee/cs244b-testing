package com.lia.scale.ReverseProxyPlayer;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;

public class Application {
    public static void main(String[] args) {
        System.out.println("hello proxy");
        try {
            // try proxy request
            // request to: 14001

            System.out.println("Message: START Listening for connection on port 14001");
            try {
                String host = "localhost";
                int remoteport = 24001;
                int localport = 14001;
                // Print a start-up message
                System.out.println("Starting proxy for " + host + ":" + remoteport
                        + " on port " + localport);
                ServerSocket server = new ServerSocket(localport);
                while (true) {
                    new HttpDataProxy(server.accept(), host, remoteport);
                }
            } catch (Exception e) {
                System.err.println(e);
                System.err.println("Usage: java ProxyMultiThread "
                        + "<host> <remoteport> <localport>");
            }
        } catch (Exception e){
            e.printStackTrace();
            System.out.println("Critical: unexpected exception");
            return;
        }
    }
}
