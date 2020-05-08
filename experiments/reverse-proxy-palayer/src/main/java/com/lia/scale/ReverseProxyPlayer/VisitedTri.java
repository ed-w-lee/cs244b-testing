package com.lia.scale.ReverseProxyPlayer;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

public class VisitedTri {

    static int INI_MAP_SIZE = 256;
    private static final VisitedTri singleton = new VisitedTri();
    static Map<String, Integer> variants = new HashMap<>();
    static int variantID = 0, lastOne = 0, lastTwo = 0;
    static int[][][] visitedTri = new int[INI_MAP_SIZE][INI_MAP_SIZE][INI_MAP_SIZE];
    static long requestCount = 0;


    public static VisitedTri getVisitedTri(){
        return singleton;
    }

    public static int add(String part){
        int nowCount = 0;
        synchronized (singleton){
            if (!variants.containsKey(part)){
                variants.put(part, variantID++);
            }
            int id = variants.get(part);
            nowCount = visitedTri[lastTwo][lastOne][id]++;
            lastTwo = lastOne;
            lastOne = id;
            requestCount++;
        }
        int requestBuckets = variants.size()*variants.size()*variants.size();
        if (nowCount < requestCount/requestBuckets){
            return 0;
        }
        return (int)(nowCount - requestCount/requestBuckets);
    }

}
