package com.lia.scale.ReverseProxyPlayer;

import java.beans.IntrospectionException;
import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;

public class Application {
    private static final int MAX_CONNECTIONS = 1000;

    public static void main(String[] args) {
        try {
            // try proxy request
            // request to: 14001
            Map<String, String> params = new HashMap<>();
            for (int i = 0; 2*i+1 < args.length; i++){
                params.put(args[2*i], args[2*i+1]);
            }

            //System.out.println("Message: START Listening for connection on port 14001");
            try {
                String[] remoteLocalArr = params.get("-p").split(":");
                final int localport = Integer.valueOf(remoteLocalArr[0]);
                final int remoteport = Integer.valueOf(remoteLocalArr[1]);
                for (String remoteLocal : params.get("-s").split(",")){
                    // Print a start-up message
                    new Thread() {
                        public void run() {
                            try{
                                String host = "127.0.0." + remoteLocal;
                                System.out.println("Starting proxy for " + host + ":" + remoteport + " on port " + localport);
                                InetAddress localAddress = InetAddress.getByName(host);
                                ServerSocket server = new ServerSocket( localport, MAX_CONNECTIONS, localAddress);
                                while (true) {
                                    new HttpDataProxy(server.accept(), host, remoteport, params);
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
                System.err.println("Usage: java ReverseProxyPlayer "
                        + "-p 14001:24001 "
                        + "-s 10,11,12 "
                        + "-v {no,rnd,exp,tree}");
            }
        } catch (Exception e){
            e.printStackTrace();
            System.out.println("Critical: unexpected exception");
            return;
        }
    }
}
