package com.lia.scale.ReverseProxyPlayer;

import org.junit.Test;

import java.io.File;
import java.util.Scanner;
import java.util.Stack;

import static org.junit.Assert.*;

public class VisitedTriBlackTest {

    @Test
    public void addBlackbox_2pc_com500_base() {
        String fileName ="src\\test\\resources\\2pc_com500_base.txt";
        System.out.printf("Reading from %s \n", fileName);
        try {
            File file = new File(fileName);
            Scanner sc = new Scanner(file);
            while (sc.hasNextLine()){
                String readLine = sc.nextLine();
                byte[] inData = readLine.getBytes();
                int delay = VisitedTri.addBlackbox(inData, 0, inData.length);
                System.out.printf("%.16s\t%d\n", readLine, delay);
            }
        } catch (Exception e){
            System.out.printf("Error: exception - %s", e.getMessage());
        }
        System.out.printf("Reading done\n");
    }
}