package com.lia.scale.twophasecommit;

import com.lia.scale.twophasecommit.controller.Database;
import org.junit.Test;
import org.springframework.boot.json.JsonParser;
import org.springframework.boot.json.JsonParserFactory;
import org.springframework.http.ResponseEntity;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.ServerSocket;
import java.net.URL;
import java.time.Duration;
import java.time.ZonedDateTime;
import java.util.HashMap;
import java.util.Map;
import java.util.Random;

import static com.lia.scale.twophasecommit.TestOneThread.map2json;
import static org.assertj.core.api.Assertions.assertThat;

public class PerformanceTest {
    @Test
    public void run2NodesAndCompare() throws Exception {

        Database dbNode1 = new Database();
        dbNode1.SERVER_MAX_FULLLRETRY = 20;
        dbNode1.SERVER_RETRY = 5;


        int numberOfUsers = 200;

        int startID = 0, endID = 10000;
        String host = "127.0.0.";
        String com = "/2pc";
        int startServer = 10;
        int port = 14001;
        int serverNumber = 3;
        Thread[] activeUsers = new Thread[numberOfUsers];
        for (int userId = 0; userId < numberOfUsers; userId++){
            // Print a start-up message
            final int threadId = userId;
            final String keyName = "c" + userId;
            activeUsers[userId]  = new Thread() {
                public void run() {
                    try{
                        Map<String, String> localMemory = new HashMap<>();
                        for (int i = startID; i < endID; i++){
                            localMemory.put(keyName,String.valueOf(i));
                            int setServer = startServer + (i%serverNumber);
                            int readServer = startServer + ((i+1)%serverNumber);
                            ResponseEntity res = dbNode1.putToUrl(host + setServer, port, com, map2json(localMemory), true);
                            int reponseCodeId = res.getStatusCode().value();
                            assertThat(reponseCodeId==200 || reponseCodeId == 304).isTrue();
                            if ( reponseCodeId == 200 ){
                                String valOnServer = dbNode1.getValFromServer(host + readServer, port, com, keyName);
                                //System.out.printf("%d:  W%sR%s, %s <- %d -> %s\n", threadId, host + setServer, host + readServer, keyName, i, valOnServer);
                                assertThat(Integer.valueOf(valOnServer)).isEqualTo(i);
                            }
                        }
                    } catch (Exception e) {
                        System.out.println("MAIN THREAD Exception");
                        e.printStackTrace();
                    }
                }
            };
            activeUsers[userId].start();
            Thread.sleep(1000);
        }

        boolean isActive = true;
        int maxDeltaInMinutes = 3*60;
        ZonedDateTime start = ZonedDateTime.now();
        while (isActive && Duration.between(start, ZonedDateTime.now()).toMinutes() < maxDeltaInMinutes){
            isActive = false;
            for (int i = 0; !isActive && i < numberOfUsers; i++){
                isActive = activeUsers[i].isAlive();
            }
            Thread.sleep(1000);
        }
    }
}
