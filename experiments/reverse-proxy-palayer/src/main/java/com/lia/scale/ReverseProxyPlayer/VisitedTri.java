package com.lia.scale.ReverseProxyPlayer;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

public class VisitedTri {

    private static final VisitedTri singleton = new VisitedTri();

    static int last1 = 0, last2 = 0, last3 = 0, last4 = 0;
    static long requestCount = 0, requestBuckets = 1;

    // black box visited
    static int BLACK_MESSAGE_MAX_COUNT = 32;
    static int INI_CHUNK_MAX_SIZE = 32;
    static int SLIDING_WINDOW_SIZE = 100;
    static int SLIDING_WINDOW_TRASHOLD = 10;
    static int SLIDING_WINDOW_STRING_MIN_SIZE = 5;
    static Map<String, Integer> blackVisitedStatesCount = new HashMap<>();
    static String[] blackNewMessages = new String[SLIDING_WINDOW_SIZE];
    static int blackNewMessageLastId = 0;
    static int blackMessageCountId = 0;
    static int[] blackMessageCount = new int[BLACK_MESSAGE_MAX_COUNT];
    static int[][][][][] visitedTri = new int[BLACK_MESSAGE_MAX_COUNT][BLACK_MESSAGE_MAX_COUNT][BLACK_MESSAGE_MAX_COUNT][BLACK_MESSAGE_MAX_COUNT][BLACK_MESSAGE_MAX_COUNT];
    static int[] EMPTY_RESPONSE = new int[]{0,0};

    public static VisitedTri getVisitedTri() {
        return singleton;
    }

    public static int[] add(byte[] inData, int offset, int bytes_read)  {
        int nowCount = 0;
        int id = addBlackbox(inData, offset, bytes_read);
        if (id >= 0){
            synchronized (singleton) {
                nowCount = visitedTri[last4][last3][last2][last1][id]++;
                last4 = last3;
                last3 = last2;
                last2 = last1;
                last1 = id;
                requestCount++;
            }
        }
        if (nowCount < requestCount / requestBuckets) {
            return EMPTY_RESPONSE;
        }
        return  new int[]{id, (int) (nowCount - requestCount / requestBuckets)};
    }

    public static int addBlackbox(byte[] inData, int offset, int bytes_read) {
        // take INI_CHUNK_MAX_SIZE (32bytes) and build hash from it
        // sliding window SLIDING_WINDOW_SIZE (100) operations
        // search for 10+ operations in each slding window and add them to history
        // add to sliding window only if not found now map


        if (bytes_read < SLIDING_WINDOW_STRING_MIN_SIZE) {
            return 0;
        }

        int res = -1;

        String blackString = new String(inData, offset,
                bytes_read > INI_CHUNK_MAX_SIZE ? INI_CHUNK_MAX_SIZE : bytes_read)
                .replace("\r\n", " ");

        synchronized (singleton) {
            if (blackVisitedStatesCount.containsKey(blackString)) {
                res = blackVisitedStatesCount.get(blackString);
                blackMessageCount[res]++;
            } else {
                if (blackNewMessageLastId < SLIDING_WINDOW_SIZE) {
                    blackNewMessages[blackNewMessageLastId++] = blackString;
                } else {
                    Arrays.sort(blackNewMessages);
                    String csNowRepeatedString = "";
                    int nowValueCount = 0;
                    for (int i = 0; i < blackNewMessages.length; i++) {
                        String nowString = blackNewMessages[i];
                        if (csNowRepeatedString.equals(nowString) && (i + 1) < blackNewMessages.length) {
                            nowValueCount++;
                        } else {
                            if (nowValueCount > SLIDING_WINDOW_TRASHOLD) {
                                blackboxNewMessage(csNowRepeatedString, nowValueCount);
                            }
                            csNowRepeatedString = nowString;
                            nowValueCount = 1;
                        }
                    }
                    blackNewMessageLastId = 0;
                }
            }
        }
        return res;
    }

    public static int blackboxNewMessage(String message, int iniCount) {
        if (blackMessageCountId < blackMessageCount.length){
            requestBuckets++;
            blackVisitedStatesCount.put(message, blackMessageCountId);
            requestBuckets = blackVisitedStatesCount.size()*blackVisitedStatesCount.size()*blackVisitedStatesCount.size()
                    *blackVisitedStatesCount.size()*blackVisitedStatesCount.size();
            blackMessageCount[blackMessageCountId] = iniCount;
            return blackMessageCountId++;
        } else {
            System.out.printf("WARNING: no memory for new message %d\t%s\n", iniCount, blackMessageCountId);
        }
        return blackMessageCountId;
    }
}
