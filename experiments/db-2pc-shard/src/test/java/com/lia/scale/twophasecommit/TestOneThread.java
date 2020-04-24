package com.lia.scale.twophasecommit;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.lia.scale.twophasecommit.controller.Database;
import org.junit.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.json.JsonParser;
import org.springframework.boot.json.JsonParserFactory;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;

import java.util.HashMap;
import java.util.Map;

import static org.assertj.core.api.Assertions.assertThat;


@SpringBootTest
public class TestOneThread {
    @Autowired

    @Test
    public void contexLoads() throws Exception {
        Database dbNode1 = new Database();
        dbNode1.SERVER_PORT = 24001;
        dbNode1.SERVER_RETRY = 5;
        dbNode1.SERVER_LIST = "24001";
        assertThat(dbNode1).isNotNull();

        String res = dbNode1.memoryGet();
        assertThat(res.equals("{}")).isTrue();

        Map<String, String> localMemory = new HashMap<>();

        localMemory.put("a","1");
        assertThat(updateMemoryAndCheck(localMemory, dbNode1)).isTrue();

        localMemory.put("b","10");
        assertThat(updateMemoryAndCheck(localMemory, dbNode1)).isTrue();

        localMemory.replace("a","5");
        assertThat(updateMemoryAndCheck(localMemory, dbNode1)).isTrue();
    }

    public boolean updateMemoryAndCheck(Map<String, String> input, Database dbNode) throws Exception {
        String jsonRequest = map2json(input);
        ResponseEntity response = dbNode.memorySet(jsonRequest);
        assertThat(response.getStatusCode() == HttpStatus.OK).isTrue();

        String res = dbNode.memoryGet();
        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> resMemory = stringParser.parseMap(res);

        for (Map.Entry<String,String> v : input.entrySet()){
            assertThat(((String)resMemory.get(v.getKey())).equals(v.getValue())).isTrue();
        }
        return true;
    }

    public String map2json(Map<String, String> input) throws Exception {
        ObjectMapper objectMapper = new ObjectMapper();
        return objectMapper.writeValueAsString(input);
    }


}
