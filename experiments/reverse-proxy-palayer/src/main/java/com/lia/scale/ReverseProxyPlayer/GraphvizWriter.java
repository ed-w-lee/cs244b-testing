package com.lia.scale.ReverseProxyPlayer;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashMap;
import java.util.Map;
import java.util.Stack;

public class GraphvizWriter {
    public static void printToFile(NodeVisited root, String outputFileName) {
        try {
            FileWriter fileWriter = new FileWriter(outputFileName + ".gv");
            PrintWriter printWriter = new PrintWriter(fileWriter);
            printWriter.printf("digraph VISITED{\n");
            printWriter.printf("\tbgcolor=white;");

            Stack<NodeVisited> backlog = new Stack<>();
            backlog.push(root);

            Map<String, String> nodeColors = new HashMap<>();

            while (!backlog.isEmpty()){
                NodeVisited nowNode = backlog.pop();
                String nodeName = nowNode.codeName;
                if (!nodeColors.containsKey(nodeName)){
                    String newColor = ColorPool.getNewColor(true);
                    String label = String.format("%s \\n success:%d \\n drop:%d", nodeName
                            , nowNode.getSuccessCount(), nowNode.getFailureCount());

                    printNode(nodeName, newColor, label, printWriter);
                    nodeColors.put(nodeName, newColor);
                }
                String nodeColor = nodeColors.get(nodeName);
                for ( NodeVisited childNode : nowNode.children){
                    if (childNode != null){
                        printNodeTransition(nodeName, childNode.getCodeName(), nodeColor, printWriter);
                        backlog.push(childNode);
                    }
                }

            }

            printWriter.printf("}\n");
            printWriter.close();

        }catch (Exception e){
            e.printStackTrace();
        }

    }

    static void printNodeTransition(String fromNode, String toNode, String color, PrintWriter printWriter)
    {
        printWriter.printf("\t\"%s\" -> \"%s\" [color=%s,fontname=\"Arial-Italic\", fontsize=8];\n"
                , fromNode, toNode, color);

    }

    static void printNode(String nodeName, String color, String label, PrintWriter printWriter)
    {
        printWriter.printf("\"%s\" [fontname=\"Arial\",color=%s,label=\"%s\"];\n"
                , nodeName, color, label);

    }

}
