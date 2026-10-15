package app.db;
import app.util.PasswordUtil;
import java.util.ArrayList;
import java.util.List;
public class MockDatabase implements Database {
    private String[] pcStatus = {"FREE", "FREE", "BUSY", "FREE", "FREE", "FREE"};

    public List<String[]> query(String sql) {
        List<String[]> rows = new ArrayList<>();
        String s = sql.toLowerCase();
        if (s.contains("from users")) {
            String h = PasswordUtil.hash("1234");
            rows.add(new String[]{"1", "Priyanshu", h, "CUSTOMER", "500"});
            rows.add(new String[]{"2", "Shifa", h, "ADMIN", "0"});
            rows.add(new String[]{"3", "Gourvansh", h, "STAFF", "0"});
        } else if (s.contains("from pcs")) {
            for (int i = 0; i < pcStatus.length; i++) {
                rows.add(new String[]{String.valueOf(i + 1), "PC-" + (i + 1), pcStatus[i]});
            }
        }
        return rows;
    }

    public void execute(String sql) {
        String s = sql.toLowerCase();
        if (s.startsWith("update pcs")) {
            int id = Integer.parseInt(s.replaceAll(".*where id = (\\d+).*", "$1"));
            pcStatus[id - 1] = s.contains("'busy'") ? "BUSY" : "FREE";
        }
        System.out.println("MOCK EXEC: " + sql);
    }

    public void close() {}
}
