#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> // open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, O_APPEND
const char *sysname = "shellish";

enum return_codes
{
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t
{
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next)
  {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
  if (command->arg_count)
  {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next)
  {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL)
  {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  }
  else
  {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1)
  {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue;                                          // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0)
    {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>')
    {
      if (len > 1 && arg[1] == '>')
      {
        redirect_index = 2;
        arg++;
        len--;
      }
      else
        redirect_index = 1;
    }
    if (redirect_index != -1)
    {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace()
{
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1)
  {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0)
      {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68)
    {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0)
      {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}
/*
 * Bu fonksiyon, kullanıcıdan gelen komutun çalıştırılabilir dosya yolunu bulur.
 *
 * Amaç:
 * - Eğer komut zaten bir path içeriyorsa (örn: /bin/ls veya ./a.out),
 *   doğrudan onun çalıştırılabilir olup olmadığını kontrol etmek
 * - Eğer komut sadece isim olarak geldiyse (örn: ls),
 *   PATH ortam değişkenindeki klasörlerin içinde sırayla aramak
 *
 * Dönüş değeri:
 * - Komut bulunursa çalıştırılabilir dosyanın tam yolunu döndürür
 *   (örn: /usr/bin/ls)
 * - Bulunamazsa NULL döndürür
 *
 * Not:
 * - Dönen string dinamik olarak ayrılmıştır (malloc / strdup).
 * - İş bittikten sonra free() ile serbest bırakılmalıdır.
 */
char *resolve_executable_path(const char *cmd)
{
  // Eğer komut NULL ise veya boş string ise arama yapamayız.
  if (cmd == NULL || strlen(cmd) == 0)
    return NULL;

  // Eğer komutun içinde '/' karakteri varsa, bu komut büyük ihtimalle
  // zaten bir path olarak verilmiştir. (/bin/ls, ./program, ../test/a.out)
  if (strchr(cmd, '/'))
  {
    // access(..., X_OK) dosyanın çalıştırılabilir olup olmadığını kontrol eder.
    if (access(cmd, X_OK) == 0)
      // Çalıştırılabiliyorsa bu path'in bir kopyasını döndür.
      return strdup(cmd);

    // Çalıştırılamıyorsa bulunamadı gibi davran.
    return NULL;
  }

  // PATH ortam değişkenini al.
  // PATH içinde komutların aranacağı klasörler bulunur.
  // Örnek:
  // /usr/local/bin:/usr/bin:/bin
  char *path_env = getenv("PATH");

  // Eğer PATH yoksa arama yapamayız.
  if (path_env == NULL)
    return NULL;

  // strtok string'i değiştirdiği için PATH'in direkt kendisini parçalamıyoruz.
  // Önce bir kopyasını alıyoruz.
  char *path_copy = strdup(path_env);

  // Kopya oluşturulamadıysa NULL dön.
  if (path_copy == NULL)
    return NULL;

  // PATH'i ':' karakterine göre parçala.
  // İlk klasörü al.
  char *dir = strtok(path_copy, ":");

  // Tüm PATH klasörlerinde sırayla dolaş.
  while (dir != NULL)
  {
    // Oluşturacağımız tam path için gerekli uzunluğu hesapla.
    // +2 sebebi:
    // 1 karakter '/' için
    // 1 karakter '\0' string sonu için
    size_t full_len = strlen(dir) + strlen(cmd) + 2;

    // Tam path string'i için bellek ayır.
    char *full_path = (char *)malloc(full_len);

    // Bellek ayrılamadıysa önce path_copy'yi temizle, sonra çık.
    if (full_path == NULL)
    {
      free(path_copy);
      return NULL;
    }

    // "klasör/komut" şeklinde tam path oluştur.
    // Örnek:
    // dir = /usr/bin
    // cmd = ls
    // sonuç = /usr/bin/ls
    snprintf(full_path, full_len, "%s/%s", dir, cmd);

    // Oluşturulan dosya gerçekten çalıştırılabilir mi kontrol et.
    if (access(full_path, X_OK) == 0)
    {
      // PATH kopyasına artık ihtiyaç yok, temizle.
      free(path_copy);

      // Bulduğumuz tam yolu döndür.
      return full_path;
    }

    // Bu klasörde bulunamadıysa bu path'i temizle.
    free(full_path);

    // PATH içindeki bir sonraki klasöre geç.
    dir = strtok(NULL, ":");
  }

  // Hiçbir klasörde bulunamadıysa PATH kopyasını temizle.
  free(path_copy);

  // Komut bulunamadı.
  return NULL;
}

int process_command(struct command_t *command)
{
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0)
  {
    if (command->arg_count > 0)
    {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    // Kullanıcının yazdığı komutun gerçek çalıştırılabilir dosya yolunu bul.
    // Örnek:
    // "ls"   -> "/usr/bin/ls"
    // "date" -> "/usr/bin/date"
    // "./a.out" -> "./a.out" (eğer çalıştırılabilirse)
    char *resolved_path = resolve_executable_path(command->name);

    // Eğer komut PATH içinde veya verilen path'te bulunamadıysa,
    // kullanıcıya "command not found" mesajı ver ve child process'i hata koduyla bitir.
    if (resolved_path == NULL)
    {
      printf("-%s: %s: command not found\n", sysname, command->name);
      exit(127);
    }

    /* ===== Part 2: I/O Redirection (Kısa açıklamalı) =====
     * redirects[0] = "<input"   -> stdin dosyadan okunsun
     * redirects[1] = ">output"  -> stdout dosyaya yazılsın (truncate)
     * redirects[2] = ">>output" -> stdout dosyaya yazılsın (append)
     * ===================================================== */

     
    // <input : stdin'i dosyadan okumak için yönlendirme
    if (command->redirects[0])
    {                                                    // Eğer "<" ile input dosyası verilmişse
      int in_fd = open(command->redirects[0], O_RDONLY); // Dosyayı sadece okuma modunda aç
      if (in_fd < 0)
      { // Açma başarısızsa
        printf("-%s: input dosyasi acilamadi %s: %s\n",
               sysname, command->redirects[0], strerror(errno)); // Hata mesajı yaz
        exit(1);                                                 // Child process'i hata ile bitir
      }
      dup2(in_fd, STDIN_FILENO); // stdin(0) artık bu dosyadan gelsin
      close(in_fd);              // Artık gerek kalmayan fd'yi kapat
    }

    // >output : stdout'u dosyaya yazmak (truncate: varsa içini sıfırlar)
    if (command->redirects[1])
    {                                                 // Eğer ">" ile output dosyası verilmişse
      int out_fd = open(command->redirects[1],        // Output dosyasını aç
                        O_WRONLY | O_CREAT | O_TRUNC, // yazma + yoksa oluştur + varsa sıfırla
                        0644);                        // dosya izinleri (rw-r--r--)
      if (out_fd < 0)
      { // Açma başarısızsa
        printf("-%s: output dosyasi acilamadi %s: %s\n",
               sysname, command->redirects[1], strerror(errno)); // Hata mesajı yaz
        exit(1);                                                 // Child process'i hata ile bitir
      }
      dup2(out_fd, STDOUT_FILENO); // stdout(1) artık bu dosyaya yazsın
      close(out_fd);               // Artık gerek kalmayan fd'yi kapat
    }

    // >>output : stdout'u dosyaya yazmak (append: sona ekler)
    if (command->redirects[2])
    {                                                  // Eğer ">>" ile append dosyası verilmişse
      int app_fd = open(command->redirects[2],         // Append dosyasını aç
                        O_WRONLY | O_CREAT | O_APPEND, // yazma + yoksa oluştur + sona ekle
                        0644);                         // dosya izinleri
      if (app_fd < 0)
      { // Açma başarısızsa
        printf("-%s: append dosyasi acilamadi %s: %s\n",
               sysname, command->redirects[2], strerror(errno)); // Hata mesajı yaz
        exit(1);                                                 // Child process'i hata ile bitir
      }
      dup2(app_fd, STDOUT_FILENO); // stdout(1) artık bu dosyaya (sona ekleyerek) yazsın
      close(app_fd);               // Artık gerek kalmayan fd'yi kapat
    }

    // Komutun gerçek path'i bulunduysa execv ile çalıştır.
    // resolved_path tam dosya yoludur.
    // command->args ise argüman listesidir.
    // Örnek:
    // execv("/usr/bin/ls", ["ls", "-l", NULL]);
    execv(resolved_path, command->args);

    /*
     * Eğer execv başarılı olursa bu satırların altına asla gelinmez.
     * Çünkü child process artık eski kodu bırakır ve yeni programı çalıştırmaya başlar.
     *
     * Eğer buraya gelindiyse execv başarısız olmuş demektir.
     * Bu durumda hata mesajını yazdırıyoruz.
     */
    printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));

    // resolve_executable_path içinde dinamik bellek ayırdığımız için
    // execv başarısız olursa burada belleği serbest bırakıyoruz.
    free(resolved_path);

    // Child process'i hata kodu ile sonlandır.
    exit(127);
  }
  else
  {
    // Eğer komut background olarak çalıştırılacaksa parent beklemez.
    if (command->background)
    {
      return SUCCESS;
    }

    // Foreground komutlarda parent, child process'in bitmesini bekler.
    waitpid(pid, NULL, 0);
    return SUCCESS;
  }
}

int main()
{
  while (1)
  {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
