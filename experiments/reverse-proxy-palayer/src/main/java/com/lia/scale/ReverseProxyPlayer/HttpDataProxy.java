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

    private static Pattern patternHTTP_COM_TRID = Pattern.compile(".*PUT (/\\w+).+trid\":\"(\\w+)\".*", Pattern.DOTALL);
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
                        while ((bytes_read = inFromClient.read(request)) != -1){
                            String requestString = new String(request,0, bytes_read);
                            //System.out.printf("REQUEST:%d: %s\n", threadId, requestString);
                            //REQUEST:11: GET /2pc
                            //REQUEST:11: PUT /2pc HTTP/1.1
                            //REQUEST:13: {"a":"1","trid":"24001"}
                            //REQUEST:13: PUT /vote HTTP/1.1
                            //REQUEST:13: PUT /commit HTTP/1.1
                            //REQUEST:13: PUT /apply HTTP/1.1

                            int sleepFromVisited = 0;
                            String[] parsedComTrId = parsePartTrId(requestString);
                            if (parsedComTrId != null && parsedComTrId.length == 2){
                                String part = String.format("%d,%s", serverPort, parsedComTrId[0]);
                                if ( visitedMode.equals(VISITED_RND) ){
                                    sleepFromVisited = randomGenerator.nextInt(timeLimit);
                                } else if ( visitedMode.equals(VISITED_EXP) ){
                                    sleepFromVisited = VisitedTri.add(part);
                                    sleepFromVisited *= sleepFromVisited;
                                } else if ( visitedMode.equals(VISITED_TREE) ){
                                    sleepFromVisited = VisitedTree.addSuccess(part, parsedComTrId[1]);
                                    sleepFromVisited *= sleepFromVisited;
                                }
                                System.out.printf("%s,%d\n", part, sleepFromVisited);
                            }
                            // TODO: respond to server/delay/skip?

                            if (sleepFromVisited > timeCrashTrashhold){
                                String part = String.format("%d,%s", serverPort, "internalcrash");
                                if (visitedMode.equals(VISITED_EXP)) {
                                    sleepFromVisited = VisitedTri.add(part);
                                } else if ( visitedMode.equals(VISITED_TREE) && parsedComTrId != null && parsedComTrId.length == 2){
                                    sleepFromVisited = VisitedTree.addSuccess(part, parsedComTrId[1]);
                                }
                                System.out.printf("%s,%d\n", part, sleepFromVisited);
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

    public static String[] parsePartTrId(String ex){
        Matcher matcherComTr = patternHTTP_COM_TRID.matcher(ex);
        String[] res = null;
        if (matcherComTr.find()) {
            res = new String[2];
            res[0] = matcherComTr.group(1);
            res[1] = matcherComTr.group(2);
        }
        return res;
    }
}
