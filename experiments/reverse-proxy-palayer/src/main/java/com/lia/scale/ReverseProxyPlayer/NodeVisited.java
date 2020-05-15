package com.lia.scale.ReverseProxyPlayer;

import java.util.Arrays;

public class NodeVisited {
    int code = -1;
    String codeName;
    int countSuccess = 0, countFail = 0;
    int INI_MAP_SIZE = 32;

    NodeVisited[] children = new NodeVisited[INI_MAP_SIZE];
    int[][] visitedCount = new int[INI_MAP_SIZE][INI_MAP_SIZE];

    public NodeVisited(String codeName, int code){
        this.code = code;
        this.codeName = codeName;
        this.countSuccess = 0;
        this.countFail = 0;
    }

    public int addSuccess(int childCode){
        return children[childCode].countSuccess++;
    }

    public int addFailure(int childCode){
        return children[childCode].countFail++;
    }

    public NodeVisited getChild(String childName, int childCode) {
        while (childCode >= children.length ){
            children = Arrays.copyOf(children, 2*children.length);
        }
        if (children[childCode] == null){
            children[childCode] = new NodeVisited(childName, childCode);
        }
        return children[childCode];
    }

    public void visit(int successCount, int failCount) {
        while (successCount >= visitedCount.length ){
            visitedCount = Arrays.copyOf(visitedCount, 2*visitedCount.length);
        }
        visitedCount[successCount][failCount]++;
    }

    public int getCount(int successCount, int failCount) {
        return visitedCount[successCount][failCount];
    }

    public int getSuccessCount() {
        int res = 0;
        for (int [] r : visitedCount){
            res += r[0];
        }
        return res;
    }

    public int getFailureCount() {
        int res = 0;
        for (int [] r : visitedCount){
            for (int i = 1; i < r.length; i++){
                res += r[i];
            }
        }
        return res;
    }

    public String getCodeName() {
        return codeName;
    }
}
