package com.lia.scale.ReverseProxyPlayer;

import org.junit.Test;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

import static org.junit.Assert.*;

public class VisitedTriTest {

    /*
    @Test
    public void addMany() {
        char[] nowString = "abcdef".toCharArray();
        for (int k = 0; k < 100; k++){
            int v = VisitedTri.add(String.valueOf(nowString[k % nowString.length]));
            if (k > 6){
                int b = 1;
                System.out.printf("%d %d\n", v , (k+2)/6-1);
//                assertEquals((k+2)/6-1,v);
            }
        }
    }*/

    @Test
    public void addSmall() {
        String[] nowString = new String[]{"a","b","c","d","e","a","b","c","d","e","a"};
        int[] vals = new int[]{0,0,0,0,0,0,0,0,0,1,1};
        for (int k = 0; k < vals.length; k++){
            int v = VisitedTri.add(nowString[k]);
            assertEquals(vals[k],v);
        }
    }

    @Test
    public void testRegularMatchComTr() {
        String ex1 = "PUT /vote HTTP/1.1\n\r" +
                "Content-Type: application/json; utf-8\n\r" +
                "Accept: application/json\n\r" +
                "User-Agent: Java/1.8.0_211\n\r" +
                "Host: localhost:14001\n\r" +
                "Connection: keep-alive\n\r" +
                "Content-Length: 52\n\r" +
                "\n\r" +
                "{\"qa\":\"as\",\"a\":\"1\",\"b\":\"1a\",\"z\":\"AA\",\"trid\":\"24003\"}";
        String[] res = HttpDataProxy.parsePartTrId(ex1);
        assertEquals("/vote",res[0]);
        assertEquals("24003",res[1]);
    }

    @Test
    public void testRegularMatchComTr3() {
        String ex1 = "PUT /apply HTTP/1.1\r\n" +
                "Content-Type: application/json; utf-8\r\n" +
                "Accept: application/json\r\n" +
                "User-Agent: Java/1.8.0_211\r\n" +
                "Host: localhost:14001\r\n" +
                "Connection: keep-alive\r\n" +
                "Content-Length: 52\r\n" +
                "\r\n" +
                "{\"qa\":\"as\",\"a\":\"1\",\"b\":\"1a\",\"z\":\"AA\",\"trid\":\"24001\"}";
        String[] res = HttpDataProxy.parsePartTrId(ex1);
        assertEquals("/apply",res[0]);
        assertEquals("24001",res[1]);
    }

    @Test
    public void testRegularMatchComTr2() {
        String ex1 = "PUT /commit HTTP/1.1\n" +
                "Content-Type: application/json; utf-8\n" +
                "Accept: application/json\n" +
                "User-Agent: Java/1.8.0_211\n" +
                "Host: localhost:14001\n" +
                "Connection: keep-alive\n" +
                "Content-Length: 52\n" +
                "\n" +
                "{\"qa\":\"as\",\"a\":\"1\",\"b\":\"1a\",\"z\":\"AA\",\"trid\":\"24003\"}";
        String[] res = HttpDataProxy.parsePartTrId(ex1);
        assertEquals("/commit",res[0]);
        assertEquals("24003",res[1]);
    }

}