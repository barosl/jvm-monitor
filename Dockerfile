FROM ubuntu

WORKDIR /app

RUN apt-get update
RUN apt-get -y --no-install-recommends install build-essential
#RUN apt-get -y --no-install-recommends install default-jdk
RUN DEBIAN_FRONTEND=noninteractive apt-get -y --no-install-recommends install openjdk-8-jdk
RUN apt-get -y --no-install-recommends install python3

RUN apt-get -y --no-install-recommends install python3-pip
RUN pip install fastapi
RUN pip install uvicorn
RUN pip install jinja2

COPY TestApp/TestApp.java ./
RUN javac -g TestApp.java

COPY src src
RUN export JAVA_HOME=$(java -XshowSettings:properties -version 2>&1 | grep java.home | awk '{print $3}'); \
    gcc -O2 -shared -fPIC -Wall -Werror -I$JAVA_HOME/include -I$JAVA_HOME/include/linux  -I$JAVA_HOME/../include -I$JAVA_HOME/../include/linux src/main.c

COPY cfg.txt .
COPY parse.py .
COPY web.py .
COPY tpls tpls

CMD java -agentpath:./a.out=cfg=cfg.txt TestApp && cat log.txt && python3 parse.py log.txt db.txt && uvicorn --host '' web:app
