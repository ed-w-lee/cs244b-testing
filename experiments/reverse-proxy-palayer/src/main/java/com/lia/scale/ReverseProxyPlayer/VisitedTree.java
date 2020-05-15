package com.lia.scale.ReverseProxyPlayer;

import org.w3c.dom.Node;

import java.util.HashMap;
import java.util.Map;

public class VisitedTree {

    static int MAX_TREE_DEPTH = 8;
    static String STATE_ROOT_NAME = "/root";
    private static final VisitedTree singleton = new VisitedTree();
    private static NodeVisited root = new NodeVisited(STATE_ROOT_NAME, 0);
    static Map<String, Integer> variants = new HashMap<>();
    static Map<String, NodeValue> transactionPositions = new HashMap<>();
    static int requestBuckets = 1;
    static int variantID = 1;

    static long requestCount = 0;

    public VisitedTree(){
    }

    public static VisitedTree getVisitedTri(){
        return singleton;
    }

    public static class NodeValue{
        public NodeVisited nodeLink;
        public int successCount, failCount;
        public NodeValue(NodeVisited nodeLink){
            this.nodeLink = nodeLink;
            successCount = 1;
        }
    }

    public static int addSuccess(String part, String trid, boolean isSuccess){
        int resCount = 1;
        synchronized (singleton){
            boolean isFinal = HttpDataProxy.isFinal(part);
            if (!variants.containsKey(part)){
                variants.put(part, variantID++);
                requestBuckets = variants.size()*variants.size()*variants.size()*variants.size()*variants.size();
            }
            int partID = variants.get(part);

            if (!transactionPositions.containsKey(trid)){
                if (isFinal){
                    return 0;
                }
                NodeValue childValues = new NodeValue(root.getChild(part, partID));
                transactionPositions.put(trid, childValues);
            } else {
                NodeValue nowNodeValues = transactionPositions.get(trid);
                if (nowNodeValues.nodeLink.code == partID){
                    if (isSuccess){
                        nowNodeValues.successCount++;
                    } else {
                        nowNodeValues.failCount++;
                    }
                } else {
                    nowNodeValues.nodeLink.visit(nowNodeValues.successCount, nowNodeValues.failCount);
                    nowNodeValues.nodeLink = nowNodeValues.nodeLink.getChild(part, partID);
                    nowNodeValues.successCount = 1;
                    nowNodeValues.failCount = 0;
                }
                resCount = nowNodeValues.nodeLink.getCount(nowNodeValues.successCount, nowNodeValues.failCount);
                if (isFinal){
                    transactionPositions.remove(trid);
                }
            }
            requestCount++;
            if (requestCount % 100 == 0){
                GraphvizWriter.printToFile(root, String.valueOf(requestCount));
            }
        }
        /*if (nowCount < requestCount/requestBuckets){
            return 0;
        }
        return (int)(nowCount - requestCount/requestBuckets);*/
        return resCount;
    }

}
