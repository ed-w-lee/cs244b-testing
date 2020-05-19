package com.lia.scale.ReverseProxyPlayer;

import java.io.*;
import java.net.Socket;
import java.util.HashMap;
import java.util.Map;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class HttpDataProxy extends Thread  {

    public static final String VISITED_RND = "rnd";
    public static final String VISITED_EXP = "exp";
    public static final String VISITED_TREE = "tree";

    public static final String[] STATE_FINAL_LIST = new String[]{"/apply","/abort"};

    private static Pattern patternHTTP_COM = Pattern.compile(".*PUT (/\\w+).*", Pattern.DOTALL);
    private static Pattern patternHTTP_TRID = Pattern.compile(".*trid\":\"(\\w+)\".*", Pattern.DOTALL);
    private Socket sClient;
    private String SERVER_URL = "";
    private String visitedMode = VISITED_RND;
    private int SERVER_PORT = 0;
    private Random randomGenerator = new Random();
    private int timeLimit ;
    private int timeCrashTrashhold ;
    Map<String, String> params = null;

    HttpDataProxy(Socket sClient, String ServerUrl, int ServerPort, Map<String, String> params) {
        this.SERVER_URL = ServerUrl;
        this.SERVER_PORT = ServerPort;
        this.sClient = sClient;
        this.params = params;
        this.timeLimit = Integer.valueOf(params.get("-t"));
        this.visitedMode = String.valueOf(params.get("-v"));
        this.timeCrashTrashhold = this.timeLimit * (100 - Integer.valueOf(params.get("-x"))) / 100;
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
            //System.out.printf("Message:%d: NEW proxy, tread id - %d\n", threadId, threadId);
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

            final int serverPort = server.getPort();

            new Thread() {
                public void run() {
                    int bytes_read;
                    try {
                        String[] parsedComTrId = null;
                        while ((bytes_read = inFromClient.read(request)) != -1){
                            String requestString = new String(request,0, bytes_read);
                            System.out.printf("REQUEST:%d: %s\n", threadId, requestString);
                            //REQUEST:11: GET /2pc
                            //REQUEST:11: PUT /2pc HTTP/1.1
                            //REQUEST:13: {"a":"1","trid":"24001"}
                            //REQUEST:13: PUT /vote HTTP/1.1
                            //REQUEST:13: PUT /commit HTTP/1.1
                            //REQUEST:13: PUT /apply HTTP/1.1

                            int sleepFromVisited = 0;
                            String com = "";
                            String trid = "";
                            String part = "";
                            if (parsedComTrId == null){
                                parsedComTrId = new String[2];
                            }
                            if (parsePartTrId(requestString, parsedComTrId)){
                                com = parsedComTrId[0];
                                trid = parsedComTrId[1];
                                parsedComTrId = new String[2];

                                part = String.format("%d,%s", serverPort, com);
                                //String part = com;
                                if ( visitedMode.equals(VISITED_RND) ){
                                    sleepFromVisited = randomGenerator.nextInt(timeLimit);
                                } else if ( visitedMode.equals(VISITED_EXP) ){
                                    sleepFromVisited = VisitedTri.add(part);
                                    sleepFromVisited *= sleepFromVisited;
                                } else if ( visitedMode.equals(VISITED_TREE) ){
                                    sleepFromVisited = VisitedTree.addSuccess(part, trid, true);
                                    sleepFromVisited *= sleepFromVisited;
                                }
                                System.out.printf("%d:%s,%d\n", serverPort, part, sleepFromVisited);
                            }
                            // TODO: respond to server/delay/skip?

                            if (sleepFromVisited > timeCrashTrashhold){
                                //String part = String.format("%d,%s", serverPort, "internalcrash");
                                if (visitedMode.equals(VISITED_EXP)) {
                                    part = "internalcrash";
                                    sleepFromVisited = VisitedTri.add(part);
                                    sleepFromVisited *= sleepFromVisited;
                                } else if ( visitedMode.equals(VISITED_TREE) && trid.length() > 0){
                                    sleepFromVisited = VisitedTree.addSuccess(part, trid, false);
                                    sleepFromVisited *= sleepFromVisited;
                                }
                                System.out.printf("%d:%s,%d\n", serverPort, part, sleepFromVisited);
                                Thread.sleep(sleepFromVisited);
                                outToServer.close();
                                outToClient.close();
                            } else if (sleepFromVisited > 0){
                                Thread.sleep(sleepFromVisited);
                            }
                            outToServer.write(request, 0, bytes_read);
                            outToServer.flush();
                        }
                    } catch (Exception e) {
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
                    //System.out.printf("RESPONSE:%d: %s\n", threadId, requestString);
                    // TODO: respond to server/delay/skip?

                    //Thread.sleep(randomGenerator.nextInt(20));
                    outToClient.write(reply, 0, bytes_read);
                    outToClient.flush();
                }
            } catch (Exception e) {
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
                //System.out.printf("Message:%d: CLOSE proxy, tread id - %d\n", threadId, threadId);
            }
            outToClient.close();
            sClient.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public static boolean parsePartTrId(String ex, String[] res){
        Matcher matcherCom = patternHTTP_COM.matcher(ex);
        if (matcherCom.find()) {
            res[0] = matcherCom.group(1);
        }
        Matcher matcherTrid = patternHTTP_TRID.matcher(ex);
        if (matcherTrid.find()) {
            res[1] = matcherTrid.group(1);
        }
        return res[0] != null && res[1] != null;
    }

    public static boolean isFinal(String state){
        for ( String s: STATE_FINAL_LIST ){
            if (s.equals(state)){
                return true;
            }
        }
        return false;
    }
}
