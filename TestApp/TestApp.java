public class TestApp {
    static void b() {
        int a = 123;
        long b = 456;
        float c = 99.99f;
        double d = 7.5;
        float e = 3.3f;
        System.out.println("b()");
        throw new RuntimeException("!!");
    }

    static void a(int a_arg) {
        int a_local = 111;
        b();
    }

    public static void main(String[] args) {
        double pi = 3.14;
        System.out.println("This is a TestApp");
        try {
            a(222);
        } catch (RuntimeException e2) {
            System.out.println("caught");
        }
    }
}
