package com.lia.scale.ReverseProxyPlayer;

import java.util.Arrays;

public class NodeVisited {
    int code = -1;
    int countSuccess = 0, countFail = 0;
    int INI_MAP_SIZE = 32;

    NodeVisited[] children = new NodeVisited[INI_MAP_SIZE];

    public NodeVisited(int code){
        this.code = code;
        this.countSuccess = 0;
        this.countFail = 0;
    }

    public int addSuccess(int childCode){
        NodeVisited childNode = getChild(childCode);
        return childNode.countSuccess++;
    }

    public int addFailure(int childCode){
        NodeVisited childNode = getChild(childCode);
        return childNode.countFail++;
    }

    public NodeVisited getChild(int childCode) {
        while (childCode >= children.length ){
            children = Arrays.copyOf(children, 2*children.length);
        }
        if (children[childCode] == null){
            children[childCode] = new NodeVisited(childCode);
        }
        return children[childCode];
    }
}
