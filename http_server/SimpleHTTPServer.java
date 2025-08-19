import java.net.ServerSocket;
public class SimpleHTTPServer {

  public static void main(String[] args) throws Exception {
    final ServerSocket server = new ServerSocket(8060);
    System.out.println("Listening for connection on port 8060 ....");
    while (true){
      // spin forever
    }
  }

}