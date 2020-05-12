package com.lia.scale.ReverseProxyPlayer;

import org.w3c.dom.Node;

import java.util.HashMap;
import java.util.Map;

public class VisitedTree {

    static int MAX_TREE_DEPTH = 8;
    private static final VisitedTree singleton = new VisitedTree();
    private static NodeVisited root = new NodeVisited(0);
    static Map<String, Integer> variants = new HashMap<>();
    static Map<String, NodeVisited> transactionPositions = new HashMap<>();
    static int requestBuckets = 1;
    static int variantID = 1;

    static long requestCount = 0;

    public static VisitedTree getVisitedTri(){
        return singleton;
    }

    public static int addSuccess(String part, String trid){
        NodeVisited child = null;
        synchronized (singleton){
            if (!variants.containsKey(part)){
                variants.put(part, variantID++);
                requestBuckets = variants.size()*variants.size()*variants.size()*variants.size()*variants.size();
            }
            int partID = variants.get(part);

            if (!transactionPositions.containsKey(trid)){
                child = root.getChild(partID);
                transactionPositions.put(trid, child);
            } else {
                child = transactionPositions.get(trid).getChild(partID);
                transactionPositions.replace(trid, child);
            }
            requestCount++;
        }
        /*if (nowCount < requestCount/requestBuckets){
            return 0;
        }
        return (int)(nowCount - requestCount/requestBuckets);*/
        return child.countSuccess++;
    }
}
