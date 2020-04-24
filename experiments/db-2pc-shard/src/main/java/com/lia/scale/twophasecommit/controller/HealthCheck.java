package com.lia.scale.twophasecommit.controller;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.web.bind.annotation.RequestMethod;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.bind.annotation.RequestMapping;

@RestController
public class HealthCheck {

    @RequestMapping(value="/health", method = RequestMethod.GET,produces = "application/json")
    public String index() {
        return String.format("Up, testing");
    }

}
