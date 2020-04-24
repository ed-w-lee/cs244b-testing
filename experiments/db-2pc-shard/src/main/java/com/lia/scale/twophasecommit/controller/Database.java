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

        try{
            long currentTransactionId = getNewTransactionId();
            System.out.printf("Message: Transaction CREATE  - %d.\n", currentTransactionId);

            // request VOTE for all nodes
            Map<String, String> sendMessage = new HashMap<>();
            sendMessage.put("trid",String.valueOf(currentTransactionId));

            JsonParser stringParser = JsonParserFactory.getJsonParser();
            Map<String, Object> requstVals = stringParser.parseMap(newValue);
            for(Map.Entry<String, Object> v : requstVals.entrySet()){
                sendMessage.put(v.getKey(), (String)v.getValue());
            }

            ObjectMapper objectMapper = new ObjectMapper();
            String messageToApply = objectMapper.writeValueAsString(sendMessage);
            System.out.printf("Message: VOTE START.\n");
            if (putToUrlAll("localhost", "/vote", messageToApply, false)){
                System.out.printf("Message: VOTE SUCCESS.\n");
                System.out.printf("Message: COMMIT START.\n");
                if (putToUrlAll("localhost", "/commit", messageToApply, false)){
                    System.out.printf("Message: COMMIT SUCCESS.\n");
                    System.out.printf("Message: APPLY START.\n");
                    if (putToUrlAll("localhost", "/apply", messageToApply, true)){
                        System.out.printf("Message: APPLY SUCCESS.\n");
                    } else {
                        System.out.printf("Message: NOT ALL APPLIED.\n");
                    }
                } else {
                    System.out.printf("Message: COMMIT FAIL.\n");
                    putToUrlAll("localhost", "/abort", messageToApply, true);
                    System.out.printf("Message: ABORT SENT.\n");
                }
            } else {
                System.out.printf("Message: VOTE FAIL.\n");
                putToUrlAll("localhost", "/abort", messageToApply, true);
                System.out.printf("Message: ABORT SENT.\n");
            }

        } catch (Exception e){
            System.out.printf("Error: failed to apply message -  %s.\n", newValue);
            e.printStackTrace();
            return new ResponseEntity(HttpStatus.NOT_MODIFIED);
        }

        return new ResponseEntity(HttpStatus.OK);
    }

    @RequestMapping(value="/vote", method = RequestMethod.PUT, produces = "application/json")
    @ResponseBody
    public ResponseEntity vote(@RequestBody String jsonVote) {

        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> requstVals = stringParser.parseMap(jsonVote);
        long voteTransactionId = Long.parseLong((String)requstVals.get("trid"));
        try {

            System.out.printf("Message: VOTE REQUESTED for transaction - %d.\n", voteTransactionId);
            int nowRetry = SERVER_RETRY;
            while (nowRetry-- > 0 && !currentTransactionLock.compareAndSet(-1, voteTransactionId)){
                Thread.sleep(100);
            }

            if (voteTransactionId != currentTransactionLock.get()){
                System.out.printf("Message: VOTE FAIL Transaction - %d.\n", voteTransactionId);
                return new ResponseEntity(HttpStatus.NOT_MODIFIED);
            }
            // need to start timeout for the node
            System.out.printf("Message: VOTE APPROVE Transaction - %d.\n", voteTransactionId);

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

        long currentTransactionId = Long.parseLong((String)requstVals.get("trid"));

        try {

            System.out.printf("Message: COMMIT START  - %d.\n", currentTransactionId);

            if (currentTransactionId != currentTransactionLock.get()){
                System.out.printf("Message: COMMIT FAIL, wrong transaction in request %d != %d.\n"
                        , currentTransactionId, currentTransactionLock.get());
                return new ResponseEntity(HttpStatus.NOT_MODIFIED);
            }

            memoryCommit.put(currentTransactionId, jsonMemory);

        } catch (Exception e){
            System.out.printf("Critical: Unexpected error.\n");
            e.printStackTrace();
            return new ResponseEntity(HttpStatus.INTERNAL_SERVER_ERROR);
        } finally {
            System.out.printf("Message: Transaction RELEASE - %d.\n", currentTransactionId);
            boolean releaseRes = currentTransactionLock.compareAndSet(currentTransactionId, -1);
            if (!releaseRes){
                System.out.printf("Warning: Current Transaction RELEASE %d fail, the transaction was not active.\n"
                        , currentTransactionId);
            }
        }

        return new ResponseEntity(HttpStatus.OK);
    }

    @RequestMapping(value="/apply", method = RequestMethod.PUT, produces = "application/json")
    @ResponseBody
    public ResponseEntity apply(@RequestBody String jsonMemory) {

        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> requstVals = stringParser.parseMap(jsonMemory);

        long currentTransactionId = Long.parseLong((String)requstVals.get("trid"));

        try {

            System.out.printf("Message: APPLY START  - %d.\n", currentTransactionId);

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

        long currentTransactionId = Long.parseLong((String)requstVals.get("trid"));

        try {

            System.out.printf("Message: ABORT - %d.\n", currentTransactionId);
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

    private boolean putToUrlAll(String host, String com, String message, boolean retryForever) throws Exception{
        for (String port : SERVER_LIST.split(",")){
            int nextPort = Integer.valueOf(port);
            boolean res = false;
            int retryCount = SERVER_MAX_FULLLRETRY;
            do{
                res = (putToUrl(host, nextPort, com, message).getStatusCode() == HttpStatus.OK);
                Thread.sleep(100);
            } while (!res && retryForever && retryCount-- > 0);
            if (retryCount <= 0){
                System.out.printf("Warning: last message %s.\n", message);
                System.out.printf("Warning: last host %s com %s port %d.\n", host, com, port);
                System.out.printf("Critical: Cannot retry last retryForever request, going down.\n");
                Runtime.getRuntime().exit(0);
            }
        }
        return true;
    }

    private ResponseEntity putToUrl(String host, int port, String com, String message){
        ResponseEntity responseResult = new ResponseEntity(HttpStatus.NOT_MODIFIED);
        try{
            URL url = new URL("http", host, port, com);
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
                responseResult = new ResponseEntity(HttpStatus.OK);
            }
            con.disconnect();
        } catch ( Exception e){
            e.printStackTrace();
            System.out.printf("Error: Unexpected exception\n");
        }
        return responseResult;
    }

}
