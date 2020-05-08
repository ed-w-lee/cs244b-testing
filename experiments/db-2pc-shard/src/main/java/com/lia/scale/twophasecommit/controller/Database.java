package com.lia.scale.twophasecommit.controller;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.json.JsonParser;
import org.springframework.boot.json.JsonParserFactory;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.time.LocalDateTime;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

@RestController
public class Database {

    public void Database(){
        System.out.println("CONSTRUCTOR");
    }

    @Value("${server.port}")
    public int SERVER_PORT;

    @Value("${main.nodes}")
    public String SERVER_LIST;

    @Value("${main.maxretry}")
    public Integer SERVER_RETRY;

    @Value("${main.maxfullretry}")
    public Integer SERVER_MAX_FULLLRETRY;

    AtomicLong availableTransactionId = new AtomicLong(0);
    AtomicLong currentTransactionLock = new AtomicLong(-1); // -1 memory free to use
    Map<String, String> memory = new ConcurrentHashMap<>();
    Map<Long, String> memoryCommit = new ConcurrentHashMap<>();

    public static final String emptyResponse = "{}";
    public final String TRID_FIELD_NAME = "trid";


    @RequestMapping(value="/2pc", method = RequestMethod.GET, produces = "application/json")
    public String memoryGet() {
        String resultJson = emptyResponse;
        ObjectMapper objectMapper = new ObjectMapper();

        try {
            resultJson = objectMapper.writeValueAsString(memory);
        } catch (JsonProcessingException e) {
            System.out.printf("Error: cannot prepare request, error %s.\n", e.getOriginalMessage());
            return emptyResponse;
        }
        return resultJson;
    }

    @RequestMapping(value="/2pc", method = RequestMethod.PUT, produces = "application/json")
    @ResponseBody
    public ResponseEntity memorySet(@RequestBody String newValue) {

        ResponseEntity response = new ResponseEntity(HttpStatus.NOT_MODIFIED);
        try{
            long currentTransactionId = getNewTransactionId();
            String currentTransactionIdString = getNewTransactionString(currentTransactionId);
            System.out.printf("Message: Transaction CREATE  - %s.\n", currentTransactionIdString);

            // request VOTE for all nodes
            Map<String, String> sendMessage = new HashMap<>();
            sendMessage.put(TRID_FIELD_NAME,String.valueOf(currentTransactionId));

            JsonParser stringParser = JsonParserFactory.getJsonParser();
            Map<String, Object> requstVals = stringParser.parseMap(newValue);
            for(Map.Entry<String, Object> v : requstVals.entrySet()){
                sendMessage.put(v.getKey(), (String)v.getValue());
            }

            ObjectMapper objectMapper = new ObjectMapper();
            String messageToApply = objectMapper.writeValueAsString(sendMessage);
            System.out.printf("Message: VOTE ALL_START - %s.\n", currentTransactionIdString);
            if (putToUrlAll("localhost", "/vote", messageToApply, false)){
                System.out.printf("Message: VOTE ALL_SUCCESS - %s\n", currentTransactionIdString);
                System.out.printf("Message: COMMIT ALL_START - %s.\n", currentTransactionIdString);
                if (putToUrlAll("localhost", "/commit", messageToApply, false)){
                    System.out.printf("Message: COMMIT ALL_SUCCESS - %s\n", currentTransactionIdString);
                    System.out.printf("Message: APPLY ALL_START - %s\n", currentTransactionIdString);
                    if (putToUrlAll("localhost", "/apply", messageToApply, true)){
                        System.out.printf("Message: ALL_APPLY SUCCESS - %s\n", currentTransactionIdString);
                        response = new ResponseEntity(HttpStatus.OK);
                    } else {
                        System.out.printf("Warning: NOT ALL APPLIED - %s\n", currentTransactionIdString);
                    }
                } else {
                    System.out.printf("Warning: ALL_COMMIT FAIL - %s\n", currentTransactionIdString);
                    putToUrlAll("localhost", "/abort", messageToApply, true);
                    System.out.printf("Warning: ABORT SENT - %s\n", currentTransactionIdString);
                }
            } else {
                System.out.printf("Warning: ALL_VOTE FAIL - %s\n", currentTransactionIdString);
                putToUrlAll("localhost", "/abort", messageToApply, true);
                System.out.printf("Warning: ABORT SENT - %s\n", currentTransactionIdString);
            }

        } catch (Exception e){
            System.out.printf("Error: failed to apply message -  %s.\n", newValue);
            e.printStackTrace();
        }

        return response;
    }

    @RequestMapping(value="/vote", method = RequestMethod.PUT, produces = "application/json")
    @ResponseBody
    public ResponseEntity vote(@RequestBody String jsonVote) {

        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> requstVals = stringParser.parseMap(jsonVote);
        long voteTransactionId = Long.parseLong((String)requstVals.get(TRID_FIELD_NAME));
        try {

            System.out.printf("Message: VOTE REQUESTED for transaction - %s.\n", getNewTransactionString(voteTransactionId));
            int nowRetry = SERVER_RETRY;
            while (nowRetry-- > 0 && !currentTransactionLock.compareAndSet(-1, voteTransactionId)){
                Thread.sleep(17*(SERVER_RETRY-nowRetry));
                System.out.printf("Warning: retry - %d vote for %s\n", nowRetry, getNewTransactionString(voteTransactionId));
            }

            if (voteTransactionId != currentTransactionLock.get()){
                System.out.printf("Warning: VOTE FAIL Transaction - %s.\n", getNewTransactionString(voteTransactionId));
                return new ResponseEntity(HttpStatus.NOT_MODIFIED);
            }
            // need to start timeout for the node
            System.out.printf("Message: VOTE APPROVE Transaction - %s.\n", getNewTransactionString(voteTransactionId));

        } catch (Exception e){
            System.out.printf("Critical: Unexpected error.\n");
            e.printStackTrace();
            currentTransactionLock.compareAndSet(voteTransactionId, -1);
            return new ResponseEntity(HttpStatus.INTERNAL_SERVER_ERROR);
        }

        return new ResponseEntity(HttpStatus.OK);
    }

    @RequestMapping(value="/commit", method = RequestMethod.PUT, produces = "application/json")
    @ResponseBody
    public ResponseEntity commit(@RequestBody String jsonMemory) {

        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> requstVals = stringParser.parseMap(jsonMemory);

        long currentTransactionId = Long.parseLong((String)requstVals.get(TRID_FIELD_NAME));

        try {

            System.out.printf("Message: COMMIT START  - %s.\n", getNewTransactionString(currentTransactionId));

            if (currentTransactionId != currentTransactionLock.get()){
                System.out.printf("Warning: COMMIT FAIL, wrong transaction in request %s != %s.\n"
                        , getNewTransactionString(currentTransactionId), getNewTransactionString(currentTransactionLock.get()));
                return new ResponseEntity(HttpStatus.NOT_MODIFIED);
            }

            memoryCommit.put(currentTransactionId, jsonMemory);

        } catch (Exception e){
            System.out.printf("Critical: Unexpected error.\n");
            e.printStackTrace();
            return new ResponseEntity(HttpStatus.INTERNAL_SERVER_ERROR);
        } finally {
            System.out.printf("Message: Transaction RELEASE - %s.\n", getNewTransactionString(currentTransactionId));
            boolean releaseRes = currentTransactionLock.compareAndSet(currentTransactionId, -1);
            if (!releaseRes){
                System.out.printf("Warning: Current Transaction RELEASE %s fail, the transaction was not active.\n"
                        , getNewTransactionString(currentTransactionId));
            }
        }

        return new ResponseEntity(HttpStatus.OK);
    }

    @RequestMapping(value="/apply", method = RequestMethod.PUT, produces = "application/json")
    @ResponseBody
    public ResponseEntity apply(@RequestBody String jsonMemory) {

        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> requstVals = stringParser.parseMap(jsonMemory);

        long currentTransactionId = Long.parseLong((String)requstVals.get(TRID_FIELD_NAME));

        try {

            System.out.printf("Message: APPLY START  - %s.\n", getNewTransactionString(currentTransactionId));
            if (!memoryCommit.containsKey(currentTransactionId)){
                System.out.printf("Warning: APPLY FAILED, cannot find the transaction in log  - %s.\n", getNewTransactionString(currentTransactionId));
                return new ResponseEntity(HttpStatus.NOT_MODIFIED);
            }

            for (Map.Entry<String, Object> value : requstVals.entrySet()){
                String varName = value.getKey();
                String varValue = (String)value.getValue();
                if (memory.containsKey(varName)){
                    memory.replace(varName, varValue);
                } else {
                    memory.put(varName, varValue);
                }
                System.out.printf("Message: value -  %s, set - %s.\n", varName, varValue);
            }
            memoryCommit.remove(currentTransactionId);

        } catch (Exception e){
            System.out.printf("Critical: Unexpected error.\n");
            e.printStackTrace();
            return new ResponseEntity(HttpStatus.INTERNAL_SERVER_ERROR);
        }

        return new ResponseEntity(HttpStatus.OK);
    }

    @RequestMapping(value="/abort", method = RequestMethod.PUT, produces = "application/json")
    @ResponseBody
    public ResponseEntity abort(@RequestBody String jsonMemory) {

        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> requstVals = stringParser.parseMap(jsonMemory);

        long currentTransactionId = Long.parseLong((String)requstVals.get(TRID_FIELD_NAME));

        try {

            System.out.printf("Warning: ABORT - %s.\n", getNewTransactionString(currentTransactionId));
            currentTransactionLock.compareAndSet(currentTransactionId, -1);
            memoryCommit.remove(currentTransactionId);

        } catch (Exception e){
            System.out.printf("Critical: Unexpected error.\n");
            e.printStackTrace();
            return new ResponseEntity(HttpStatus.INTERNAL_SERVER_ERROR);
        }

        return new ResponseEntity(HttpStatus.OK);
    }

    private long getNewTransactionId() {
        return ((availableTransactionId.getAndAdd(1) << 16) | SERVER_PORT);
    }

    private String getNewTransactionString(long trid) {
        if (trid > 0){
            long a = trid % (1<<16);
            long b = (trid / (1<<16));
            return String.format("%d-%d", (trid % (1<<16)), (trid / (1<<16)));
        } else {
            return String.valueOf(trid);
        }
    }

    private boolean putToUrlAll(String host, String com, String message, boolean retryForever) throws Exception{
        boolean res = false;
        for (String port : SERVER_LIST.split(",")){
            int nextPort = Integer.valueOf(port);
            res = (putToUrl(host, nextPort, com, message, retryForever).getStatusCode() == HttpStatus.OK);

            if (!res){
                System.out.printf("Warning: last message %s.\n", message);
                System.out.printf("Warning: last host %s com %s port %d.\n", host, com, nextPort);
                System.out.printf("Critical: Cannot retry last retryForever request, going down.\n");
                return false;
                //Runtime.getRuntime().exit(0);
            }
        }
        return res;
    }

    public ResponseEntity putToUrl(String host, int port, String com, String message, boolean retryForever){
        try{
            URL url = new URL("http", host, port, com);
            int retryCount = SERVER_MAX_FULLLRETRY;
            do{
                HttpURLConnection con = (HttpURLConnection) url.openConnection();
                con.setRequestMethod("PUT");
                con.setRequestProperty("Content-Type", "application/json; utf-8");
                con.setRequestProperty("Accept", "application/json");
                con.setDoOutput(true);
                try(OutputStream os = con.getOutputStream()) {
                    byte[] input = message.getBytes("utf-8");
                    os.write(input, 0, input.length);
                }
                con.setConnectTimeout(4000);
                con.setReadTimeout(4000);
                int responseStatus = con.getResponseCode();
                if (responseStatus == 200) {
                    con.disconnect();
                    return  new ResponseEntity(HttpStatus.OK);
                }
                con.disconnect();
                Thread.sleep(17*(SERVER_MAX_FULLLRETRY-retryCount));
                System.out.printf("Warning: retry %d host %s com %s port %d.\n", retryCount, host, com, port);
            }while (retryForever && retryCount-- > 0);
        } catch ( Exception e) {
            e.printStackTrace();
            System.out.printf("Error: Unexpected exception\n");
        }
        return new ResponseEntity(HttpStatus.NOT_MODIFIED);
    }

    public String getValFromServer(String host, int port, String com, String key) throws IOException {
        String res = "";
        URL url = new URL("http", host, port, com);
        HttpURLConnection con = (HttpURLConnection) url.openConnection();
        con.setRequestMethod("GET");
        con.setConnectTimeout(4000);
        con.setReadTimeout(4000);
        int status = con.getResponseCode();
        if (status == 200) {
            BufferedReader in = new BufferedReader(
                    new InputStreamReader(con.getInputStream()));
            String inputLine;
            StringBuffer content = new StringBuffer();
            while ((inputLine = in.readLine()) != null) {
                content.append(inputLine);
            }
            in.close();
            JsonParser stringParser = JsonParserFactory.getJsonParser();
            Map<String, Object> resultJson = stringParser.parseMap(content.toString());
            // {id:"1" "host":-1}
            if (resultJson.containsKey(key)){
                res = (String)resultJson.get(key);
            }
        }
        con.disconnect();

        return res;
    }

}
