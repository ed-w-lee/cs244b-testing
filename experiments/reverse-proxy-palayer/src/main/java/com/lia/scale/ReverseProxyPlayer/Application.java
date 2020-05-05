package com.lia.scale.ReverseProxyPlayer;

import java.beans.IntrospectionException;
import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.atomic.AtomicLong;

public class Application {
    public static void main(String[] args) {
        try {
            // try proxy request
            // request to: 14001

            //System.out.println("Message: START Listening for connection on port 14001");
            try {
                String host = "localhost";
                for (String remoteLocal : args[0].split("_")){
                    // Print a start-up message
                    String[] remoteLocalArr = remoteLocal.split(":");
                    final int localport = Integer.valueOf(remoteLocalArr[0]);
                    final int remoteport = Integer.valueOf(remoteLocalArr[1]);
                    new Thread() {
                        public void run() {
                            try{
                                System.out.println("Starting proxy for " + host + ":" + remoteport + " on port " + localport);
                                ServerSocket server = new ServerSocket(localport);
                                while (true) {
                                    new HttpDataProxy(server.accept(), host, remoteport);
                                    //System.out.println("NEW ACCEPT");
                                }
                            } catch (IOException e) {
                                System.out.println("MAIN THREAD Exception");
                                e.printStackTrace();
                            }
                        }
                    }.start();
                }
            } catch (Exception e) {
                System.err.println(e);
                System.err.println("Usage: java ProxyMultiThread "
                        + "<host> <localport:remoteport,localport:remoteport>");
            }
        } catch (Exception e){
            e.printStackTrace();
            System.out.println("Critical: unexpected exception");
            return;
        }
    }
}
