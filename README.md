## jvm-monitor

`jvm-monitor` is a Java agent attached to a Java VM (virtual machine), which logs all the local variables when exceptions occur.

### Rationales

Traditionally, software debugging has been done by inspecting a program state. Whether it is done by logging useful information, or by attaching a debugger to the program, the fundamentals are the same.

Logging, however, can sometimes be inefficient. For logging to be useful, you should store all the required variables *in prior*, like:

```java
log.info("a={}, b={}, c={}, d={}", a, b, c, d);
```

But, it can be hard to anticipate what exact variables will be needed. Moreover, logging every variable possible is a bit tiresome work. You should not only list all the current variables, but also cope with the future addition and deletion of variables.

In contrast, attaching a debugger to the program is quite easy. But, it can be detrimental to software performance. It can be challenging to use debuggers on production.

### What we propose

`jvm-monitor` is a lightweight debugger-like monitor that tracks exception occurrences throughout a Java program, and if they happen, logs the local variables. Not only the local variables of the current method, but also the ones of the caller, the caller of the caller, and so on, by following the call stack.

It eliminates the preparatory work required for proper logging. Once set up, it automatically logs program states whenever exceptions happen. For those error cases where exceptions are not involved, you can manually throw a custom exception to let them be handled.

### How to use

1. Build the agent.

```shell
JAVA_HOME=$(java -XshowSettings:properties -version 2>&1 | grep java.home | awk '{print $3}')
gcc -O2 -shared -fPIC -Wall -Werror -I$JAVA_HOME/include -I$JAVA_HOME/include/linux -I$JAVA_HOME/../include -I$JAVA_HOME/../include/linux src/main.c
```

2. Create a configuration file

```
catching_cls=LTestApp;
throwing_cls=LInspect;
log_file=log.txt
```

`catching_cls` and `throwing_cls` are filters. You can determine what kinds of exceptions should be logged by setting them.

`catching_cls` indicates the location of a `catch` block. For example, when the code is as follows,

```java
class TestApp {
    void madokaMethod() {
        catch (HomuraException e) {
            // ...
        }
    }
}
```

and if you intend to log all the exceptions caught by the `TestApp` class, you can specify `catching_cls=LTestApp;` to let `jvm-monitor` do the work.

`throwing_cls` indicates the location of a `try` block. You can use it for the error cases where exceptions are not involved, but still worth logging. For example,

```java
class Inspect {
    public static void inspect() {
        try {
            throw new RuntimeException();
        } catch (RuntimeException e) { }
    }
}

class AnotherClass {
    public void madoka() {
        if (statusCode == 404) {
            // Something bad happened
            Inspect.inspect();
        }
    }
}
```

Even though nothing is actually done by the `Inspect::inspect` method, `jvm-monitor` still captures the occurrence of the exception and logs it, including all the local variables.

3. Pass the following arguments to the VM

```shell
java -agentpath:./a.out=cfg=cfg.txt TestApp
```

4. Launch the web-based log viewer

```shell
python3 parse.py <log file> db.txt && uvicorn web:app
```

### Examples

You can see the sample code in the `TestApp` directory.

You can also run a demo by invoking `docker-compose up`. Port `7942` is assigned to localhost, which provides a handy web-based log viewer.
