package com.lia.scale.ReverseProxyPlayer;

import org.junit.Test;

import java.io.File;
import java.nio.Buffer;
import java.util.*;

import static org.junit.Assert.*;

public class HttpDataProxyTest {

    @Test
    public void bucketFile() {
        //String fileToAnalyze = "C:\\johny\\tmp\\twopc\\node3_client30_request1000.log";
        //String fileToAnalyze = "C:\\johny\\tmp\\twopc\\node2_client20_request1000.log";
        String fileToAnalyze = "C:\\johny\\tmp\\twopc\\rand_node3_client200_request50.log";
        try {
            final int HISTORY_LEN = 3;
            File file = new File(fileToAnalyze);
            Scanner sc = new Scanner(file);
            Set<String> keys = new HashSet<>();
            while (sc.hasNextLine()){
                String[] nowStringArr = sc.nextLine().split(",");
                String nowString = String.format("%s,%s", nowStringArr[0], nowStringArr[1]);
                keys.add(nowString);
            }
            String[] keyArr = new String[keys.size()];
            keyArr = keys.toArray(keyArr);
            Map<String, Integer> keyMap = new HashMap<>();
            for (int i = 0; i < keyArr.length; i++){
                keyMap.put(keyArr[i], i);
            }

            Map<String, Integer> stat = new HashMap<>();
            Stack<Integer> buffer = new Stack<>();
            buffer.push(0);
            boolean rollback = false;
            while (!buffer.isEmpty()){
                if (buffer.size() == HISTORY_LEN){
                    stat.put(listToStr(buffer.iterator(), keyArr),0);
                    rollback = true;
                }
                if (rollback){
                    int lastId = buffer.pop();
                    if (lastId+1 < keyArr.length){
                        buffer.push(lastId+1);
                        rollback = false;
                    }
                } else {
                    buffer.push(0);
                }
            }


            sc = new Scanner(file);
            Queue<Integer> slidingWindows = new LinkedList<>();
            while (sc.hasNextLine()){
                String[] nowStringArr = sc.nextLine().split(",");
                String nowString = String.format("%s,%s", nowStringArr[0], nowStringArr[1]);
                slidingWindows.offer(keyMap.get(nowString));
                if (slidingWindows.size() == HISTORY_LEN){
                    String nowKey = listToStr(slidingWindows.iterator(), keyArr);
                    stat.replace(nowKey, stat.get(nowKey)+1);
                    slidingWindows.poll();
                }
            }

            for (Map.Entry<String, Integer> v : stat.entrySet()){
                System.out.printf("%s   : %d\n", v.getKey(), v.getValue());
            }


        } catch (Exception e){
            System.out.printf("Error: exception - %s", e.getMessage());
        }
    }

    String listToStr(Iterator<Integer> bufferIterator, String[] keyArr){
        StringBuilder keyBuilder = new StringBuilder();

        while (bufferIterator.hasNext()){
            keyBuilder.append(keyArr[bufferIterator.next()]);
            if (bufferIterator.hasNext()) {
                keyBuilder.append(" ->  ");
            }
        }
        return keyBuilder.toString();
    }
}