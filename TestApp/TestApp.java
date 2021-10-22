class Inspect {
    public static void inspect() {
        try { throw new Throwable(); }
        catch (Throwable e) {}
    }
}

public class TestApp {
    static void b(String text) {
        int a = 123;
        long b = 456;
        float c = 99.99f;
        double d = 7.5;
        String[] words = new String[] {"akemi", "homura"};
        System.out.println("b()");
        throw new RuntimeException("This is an exception");
    }

    static void a(int a_arg) {
        int a_local = 111;
        try {
            b("This is an argument");
        } finally {
            System.out.println("finally");
        }
    }

    static void testTheAnswer() {
        String hello = "world";
        int answerToTheUniverse = 43;

        if (answerToTheUniverse != 42) {
            Inspect.inspect();
        }
    }

    public static void main(String[] args) {
        testTheAnswer();

        System.out.println("This is TestApp");
        try {
            a(222);
        } catch (RuntimeException e2) {
            System.out.println("caught");
        }
    }
}
