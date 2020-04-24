# cs244b-testing
## db-2pc-shard
Basic implementation of in memory key-value Database with Two Phase Commit (2PC) synchronization
Codebase defult configured for 2 nodes with ports: 24001, 24002

#### Build and run
to build Jar binaries and start the nodes
'''
./gradlew bootJar
java -Dserver.port=24001 -jar ./build/libs/twophasecommit-0.1.0.jar &
java -Dserver.port=24002 -jar ./build/libs/twophasecommit-0.1.0.jar &
'''

#### Use
When 2 nodes up and runnign you can send client requests:
* PUT DATA:
** url : http://localhost:24001/2pc
** method : put
** header : Content-Type = application/json
** body : {"a":"13", "b":"194"}

* GET DATA:
** url : http://localhost:24001/2pc
** method : GET
** header : Content-Type = application/json


