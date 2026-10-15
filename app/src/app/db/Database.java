package app.db;
import java.util.List;
public interface Database {
    List<String[]> query(String sql) throws Exception;
    void execute(String sql) throws Exception;
    void close();
}
