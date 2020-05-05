package com.lia.scale.ReverseProxyPlayer;

import org.junit.Test;

import static org.junit.Assert.*;

public class VisitedTriTest {

    @Test
    public void addMany() {
        String nowString = "asd";
        for (int k = 0; k < 100000; k++){
            int v = VisitedTri.add(nowString);
            assertEquals(k,v);
        }
    }

    @Test
    public void addSmall() {
        String[] nowString = new String[]{"a","b","c","a","b","c","a","b","c"};
        int[] vals = new int[]{0,0,0,0,0,1,1,1,2};
        for (int k = 0; k < vals.length; k++){
            int v = VisitedTri.add(nowString[k]);
            assertEquals(vals[k],v);
        }
    }
}