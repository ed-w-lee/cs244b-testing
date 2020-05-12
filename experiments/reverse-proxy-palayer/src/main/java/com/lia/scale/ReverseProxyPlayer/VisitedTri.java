package com.lia.scale.ReverseProxyPlayer;

import java.util.HashMap;
import java.util.Map;
import java.util.Queue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

public class VisitedTri {

    static int INI_MAP_SIZE = 32;
    private static final VisitedTri singleton = new VisitedTri();
    static Map<String, Integer> variants = new HashMap<>();
    static int variantID = 0, last1 = 0, last2 = 0, last3 = 0, last4 = 0;
    static int[][][][][] visitedTri = new int[INI_MAP_SIZE][INI_MAP_SIZE][INI_MAP_SIZE][INI_MAP_SIZE][INI_MAP_SIZE];
    static int requestBuckets = 1;

    static long requestCount = 0;


    public static VisitedTri getVisitedTri(){
        return singleton;
    }

    public static int add(String part){
        int nowCount = 0;
        synchronized (singleton){
            if (!variants.containsKey(part)){
                variants.put(part, variantID++);
                requestBuckets = variants.size()*variants.size()*variants.size()*variants.size()*variants.size();
            }
            int id = variants.get(part);
            nowCount = visitedTri[last4][last3][last2][last1][id]++;
            last4 = last3;
            last3 = last2;
            last2 = last1;
            last1 = id;
            requestCount++;
        }
        if (nowCount < requestCount/requestBuckets){
            return 0;
        }
        return (int)(nowCount - requestCount/requestBuckets);
    }

}
