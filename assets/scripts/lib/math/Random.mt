public class Random {
    public static function nextInt(int min, int max): int {
        return __random_nextInt(min, max);
    }

    public static function nextFloat(float min, float max): float {
        return __random_nextFloat(min, max);
    }
}
