package com.lia.scale.twophasecommit;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.lia.scale.twophasecommit.controller.Database;
import org.junit.Test;
import org.springframework.boot.json.JsonParser;
import org.springframework.boot.json.JsonParserFactory;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;

import java.util.HashMap;
import java.util.Map;

import static org.assertj.core.api.Assertions.assertThat;

//@RunWith(SpringRunner.class)
@SpringBootTest(properties = "server.port=24001")
//@AutoConfigureMockMvc
public class TestOneThread {

//    @Autowired
//    private Database dbNode1;

    @Test
    public void happyPathVoteCommitApply() throws Exception {
        Database dbNode1 = new Database();
        dbNode1.SERVER_LIST = "24001";
        dbNode1.SERVER_PORT = 24001;
        dbNode1.SERVER_MAX_FULLLRETRY = 100;
        dbNode1.SERVER_RETRY = 5;

        assertThat(dbNode1).isNotNull();

        String res = dbNode1.memoryGet();
        assertThat(res.equals("{}")).isTrue();

        Map<String, String> localMemory = new HashMap<>();

        localMemory.put("a","1");
        localMemory.put(dbNode1.TRID_FIELD_NAME,"89537");

        ResponseEntity voteRes = dbNode1.vote(map2json(localMemory));
        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity commitRes = dbNode1.commit(map2json(localMemory));
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity applyRes = dbNode1.apply(map2json(localMemory));
        assertThat(applyRes.getStatusCode() == HttpStatus.OK).isTrue();

        res = dbNode1.memoryGet();
        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> resMemory = stringParser.parseMap(res);

        for (Map.Entry<String,String> v : localMemory.entrySet()){
            assertThat(((String)resMemory.get(v.getKey())).equals(v.getValue())).isTrue();
        }

    }

    @Test
    public void happyPathVoteCommitApplyDouble() throws Exception {
        Database dbNode1 = new Database();
        dbNode1.SERVER_LIST = "24001";
        dbNode1.SERVER_PORT = 24001;
        dbNode1.SERVER_MAX_FULLLRETRY = 100;
        dbNode1.SERVER_RETRY = 5;

        assertThat(dbNode1).isNotNull();

        String res = dbNode1.memoryGet();
        assertThat(res.equals("{}")).isTrue();

        Map<String, String> localMemory = new HashMap<>();

        localMemory.put("a","1");
        localMemory.put(dbNode1.TRID_FIELD_NAME,"89537");

        ResponseEntity voteRes = dbNode1.vote(map2json(localMemory));
        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity commitRes = dbNode1.commit(map2json(localMemory));
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();

        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity applyRes = dbNode1.apply(map2json(localMemory));
        assertThat(applyRes.getStatusCode() == HttpStatus.OK).isTrue();

        res = dbNode1.memoryGet();
        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> resMemory = stringParser.parseMap(res);

        for (Map.Entry<String,String> v : localMemory.entrySet()){
            assertThat(((String)resMemory.get(v.getKey())).equals(v.getValue())).isTrue();
        }

    }

    @Test
    public void happyPathVoteCommitApplyTwoInARow() throws Exception {
        Database dbNode1 = new Database();
        dbNode1.SERVER_LIST = "24001";
        dbNode1.SERVER_PORT = 24001;
        dbNode1.SERVER_MAX_FULLLRETRY = 100;
        dbNode1.SERVER_RETRY = 5;

        assertThat(dbNode1).isNotNull();

        String res = dbNode1.memoryGet();
        assertThat(res.equals("{}")).isTrue();

        Map<String, String> localMemory = new HashMap<>();

        localMemory.put("a","1");
        localMemory.put(dbNode1.TRID_FIELD_NAME,"89537");

        ResponseEntity voteRes = dbNode1.vote(map2json(localMemory));
        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity commitRes = dbNode1.commit(map2json(localMemory));
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();

        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity applyRes = dbNode1.apply(map2json(localMemory));
        assertThat(applyRes.getStatusCode() == HttpStatus.OK).isTrue();

        res = dbNode1.memoryGet();
        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> resMemory = stringParser.parseMap(res);

        for (Map.Entry<String,String> v : localMemory.entrySet()){
            assertThat(((String)resMemory.get(v.getKey())).equals(v.getValue())).isTrue();
        }

        localMemory.put("b","7");
        localMemory.put(dbNode1.TRID_FIELD_NAME,"125");

        voteRes = dbNode1.vote(map2json(localMemory));
        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        commitRes = dbNode1.commit(map2json(localMemory));
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();

        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        applyRes = dbNode1.apply(map2json(localMemory));
        assertThat(applyRes.getStatusCode() == HttpStatus.OK).isTrue();

        res = dbNode1.memoryGet();
        stringParser = JsonParserFactory.getJsonParser();
        resMemory = stringParser.parseMap(res);

        for (Map.Entry<String,String> v : localMemory.entrySet()){
            assertThat(((String)resMemory.get(v.getKey())).equals(v.getValue())).isTrue();
        }
    }

    @Test
    public void happyPathVoteCommitCommitApply() throws Exception {
        Database dbNode1 = new Database();
        dbNode1.SERVER_LIST = "24001";
        dbNode1.SERVER_PORT = 24001;
        dbNode1.SERVER_MAX_FULLLRETRY = 100;
        dbNode1.SERVER_RETRY = 5;

        assertThat(dbNode1).isNotNull();

        String res = dbNode1.memoryGet();
        assertThat(res.equals("{}")).isTrue();

        Map<String, String> localMemory = new HashMap<>();
        localMemory.put("a","1");
        localMemory.put(dbNode1.TRID_FIELD_NAME,"111");


        Map<String, String> secondMemory = new HashMap<>();
        secondMemory.put("a","2");
        secondMemory.put(dbNode1.TRID_FIELD_NAME,"222");

        ResponseEntity voteRes = dbNode1.vote(map2json(localMemory));
        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity voteResSecond = dbNode1.commit(map2json(secondMemory));
        assertThat(voteResSecond.getStatusCode() != HttpStatus.OK).isTrue();

        ResponseEntity commitRes = dbNode1.commit(map2json(localMemory));
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();

        ResponseEntity applyRes = dbNode1.apply(map2json(localMemory));
        assertThat(applyRes.getStatusCode() == HttpStatus.OK).isTrue();

        res = dbNode1.memoryGet();
        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> resMemory = stringParser.parseMap(res);

        for (Map.Entry<String,String> v : localMemory.entrySet()){
            assertThat(((String)resMemory.get(v.getKey())).equals(v.getValue())).isTrue();
        }

    }

    @Test
    public void happyPathVoteApplyApplyApply() throws Exception {
        Database dbNode1 = new Database();
        dbNode1.SERVER_LIST = "24001";
        dbNode1.SERVER_PORT = 24001;
        dbNode1.SERVER_MAX_FULLLRETRY = 100;
        dbNode1.SERVER_RETRY = 5;

        assertThat(dbNode1).isNotNull();

        String res = dbNode1.memoryGet();
        assertThat(res.equals("{}")).isTrue();

        Map<String, String> localMemory = new HashMap<>();
        localMemory.put("a","1");
        localMemory.put(dbNode1.TRID_FIELD_NAME,"111");


        Map<String, String> secondMemory = new HashMap<>();
        secondMemory.put("a","2");
        secondMemory.put(dbNode1.TRID_FIELD_NAME,"222");

        ResponseEntity applyResSecond = dbNode1.apply(map2json(secondMemory));
        assertThat(applyResSecond.getStatusCode() != HttpStatus.OK).isTrue();

        ResponseEntity voteRes = dbNode1.vote(map2json(localMemory));
        assertThat(voteRes.getStatusCode() == HttpStatus.OK).isTrue();

        applyResSecond = dbNode1.apply(map2json(secondMemory));
        assertThat(applyResSecond.getStatusCode() != HttpStatus.OK).isTrue();

        ResponseEntity commitRes = dbNode1.commit(map2json(localMemory));
        assertThat(commitRes.getStatusCode() == HttpStatus.OK).isTrue();

        applyResSecond = dbNode1.apply(map2json(secondMemory));
        assertThat(applyResSecond.getStatusCode() != HttpStatus.OK).isTrue();

        ResponseEntity applyRes = dbNode1.apply(map2json(localMemory));
        assertThat(applyRes.getStatusCode() == HttpStatus.OK).isTrue();

        res = dbNode1.memoryGet();
        JsonParser stringParser = JsonParserFactory.getJsonParser();
        Map<String, Object> resMemory = stringParser.parseMap(res);

        for (Map.Entry<String,String> v : localMemory.entrySet()){
            assertThat(((String)resMemory.get(v.getKey())).equals(v.getValue())).isTrue();
        }

    }

    public String map2json(Map<String, String> input) throws Exception {
        ObjectMapper objectMapper = new ObjectMapper();
        return objectMapper.writeValueAsString(input);
    }


}
