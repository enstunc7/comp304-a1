#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> // open(), O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC, O_APPEND

#include <dirent.h>   // opendir, readdir
#include <signal.h>   // kill, SIGTERM
#include <sys/stat.h> // mkdir, mkfifo
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

// Builtin kontrol fonksiyonu (cut/help/chatroom gibi komutlar builtin mi?)
bool is_builtin_child(const char *name);

// Builtin komut çalıştırma fonksiyonu (child içinde çağrılacak)
int run_builtin_child(struct command_t *command);

// PIPELINE çalıştırır: cmd1 | cmd2 | cmd3 ... (command->next zinciri)
// Her komut için child process oluşturur ve pipe ile birbirine bağlar.
int execute_pipeline(struct command_t *cmd)
{
  int in_fd = STDIN_FILENO; // İlk komutun input'u normal stdin (fd=0)
  int pipefd[2];            // pipefd[0]=read end, pipefd[1]=write end
  pid_t pid;                // fork sonucu child pid
  int status = 0;           // wait için status

  struct command_t *current = cmd; // Zincirde gezen pointer

  while (current != NULL)
  { // Zincirde komut olduğu sürece dön
    // Eğer son komut değilse pipe açmamız gerekiyor (çıktı bir sonraki komuta gidecek)
    if (current->next != NULL)
    {
      if (pipe(pipefd) < 0)
      {                 // pipe oluştur (başarısızsa hata)
        perror("pipe"); // sistem hata mesajı bas
        return UNKNOWN; // hata kodu dön
      }
    }
    else
    {
      // Son komutta pipe yok: işaret amaçlı -1
      pipefd[0] = -1;
      pipefd[1] = -1;
    }

    pid = fork(); // Yeni child process oluştur
    if (pid < 0)
    {                 // fork başarısızsa
      perror("fork"); // hata yaz
      return UNKNOWN; // hata dön
    }

    if (pid == 0)
    {
      // =========================
      // CHILD PROCESS
      // =========================

      // Eğer önceki komuttan gelen bir input fd varsa, stdin'e bağla
      if (in_fd != STDIN_FILENO)
      {                            // stdin değilse (yani pipe read end)
        dup2(in_fd, STDIN_FILENO); // stdin(0) artık in_fd olsun
        close(in_fd);              // eski fd'yi kapat
      }

      // Eğer son komut değilsek, stdout'u pipe'ın write end'ine bağla
      if (current->next != NULL)
      {                                 // bir sonraki komut varsa
        close(pipefd[0]);               // child read end'i kullanmayacak, kapat
        dup2(pipefd[1], STDOUT_FILENO); // stdout(1) -> pipe write
        close(pipefd[1]);               // write end artık dup edildi, kapat
      }

      // Burada istersek redirection (<,>,>>) ile pipe'ı birlikte destekleyebiliriz.
      // Şimdilik sadece pipe mantığını çalıştırıyoruz.

      // Eğer komut builtin ise (cut/help/chatroom), execv yerine builtin çalıştır
      if (is_builtin_child(current->name))
      {                                      // builtin mi kontrol et
        int br = run_builtin_child(current); // builtin'i çalıştır
        exit(br == SUCCESS ? 0 : 1);         // başarılıysa 0, değilse 1 ile çık
      }

      // Komutun gerçek çalıştırılabilir yolunu PATH içinde bul
      char *resolved_path = resolve_executable_path(current->name);
      if (resolved_path == NULL)
      { // bulunamadıysa
        printf("-%s: %s: command not found\n", sysname, current->name);
        exit(127); // child'ı hata koduyla bitir
      }

      // Komutu çalıştır (başarılıysa bu satırın altına hiç gelmez)
      execv(resolved_path, current->args);

      // Eğer buraya geldiysek execv başarısız olmuştur
      printf("-%s: %s: %s\n", sysname, current->name, strerror(errno));
      free(resolved_path); // ayrılan path'i temizle
      exit(127);           // child'ı bitir
    }

    // =========================
    // PARENT PROCESS
    // =========================

    // Parent artık eski in_fd'yi tutmasın (zincir ilerledi)
    if (in_fd != STDIN_FILENO)
    {
      close(in_fd); // önceki pipe read end kapanır
    }

    // Eğer son komut değilsek:
    // - parent write end'i kapatır
    // - next komut için in_fd = pipe read end olur
    if (current->next != NULL)
    {
      close(pipefd[1]);  // parent write end'i kapatır
      in_fd = pipefd[0]; // next komut stdin'i buradan okuyacak
    }

    current = current->next; // zincirde bir sonraki komuta geç
  }

  // Parent: tüm child process'lerin bitmesini bekle
  while (wait(&status) > 0)
  {
    // burada sadece bekliyoruz
  }

  return SUCCESS; // pipeline başarıyla tamamlandı
}

// Builtin mi? (cut/chatroom/custom burada sayılacak)
bool is_builtin_child(const char *name)
{
  if (!name)
    return false;
  return (strcmp(name, "cut") == 0) ||
         (strcmp(name, "help") == 0) ||
         (strcmp(name, "repeat") == 0) ||
         (strcmp(name, "chatroom") == 0); // chatroom'u sonra yazacağız
}

// Builtin komutu çalıştırır (child içinde çağrılacak şekilde tasarlanır)
// Başarılıysa SUCCESS, değilse UNKNOWN döner.
int run_builtin_child(struct command_t *command);

// "1,3,10" gibi alan listesini int dizisine çevirir
// count: kaç tane alan çıktığını döndürür
// Not: dönen dizi malloc/realloc ile ayrılır, sonunda free edilmelidir
int *parse_fields_list(const char *s, int *count)
{
  *count = 0; // başlangıçta 0 alan

  if (s == NULL)
    return NULL; // liste yoksa NULL

  char *copy = strdup(s); // strtok bozacağı için kopya al
  if (copy == NULL)
    return NULL; // kopya alınamazsa NULL

  int *arr = NULL;               // dinamik alan dizisi
  char *tok = strtok(copy, ","); // virgüle göre böl

  while (tok != NULL)
  {                    // token oldukça devam et
    int v = atoi(tok); // token'ı sayıya çevir
    if (v > 0)
    {                                                 // sadece 1+ değerleri kabul et
      arr = realloc(arr, sizeof(int) * (*count + 1)); // diziyi büyüt
      arr[*count] = v;                                // yeni alanı ekle
      (*count)++;                                     // sayacı artır
    }
    tok = strtok(NULL, ","); // sıradaki token
  }

  free(copy); // kopyayı temizle
  return arr; // alan dizisini döndür
}

// cut builtin: stdin'den satır okur, delimiter'a göre böler, seçilen alanları basar
int run_cut_builtin(struct command_t *command)
{
  char delim = '\t';             // varsayılan delimiter TAB
  const char *fields_str = NULL; // -f/--fields ile gelecek liste

  // Argümanları tara: -d/--delimiter ve -f/--fields
  for (int i = 1; command->args[i] != NULL; i++)
  { // args[0]=cut
    // delimiter seçeneği
    if ((strcmp(command->args[i], "-d") == 0 || strcmp(command->args[i], "--delimiter") == 0) &&
        command->args[i + 1] != NULL)
    {
      delim = command->args[i + 1][0]; // tek karakter al
      i++;                             // bir sonraki arg tüketildi
    }
    // fields seçeneği
    else if ((strcmp(command->args[i], "-f") == 0 || strcmp(command->args[i], "--fields") == 0) &&
             command->args[i + 1] != NULL)
    {
      fields_str = command->args[i + 1]; // "1,3,10" gibi liste
      i++;                               // bir sonraki arg tüketildi
    }
  }

  // -f verilmediyse hata
  if (fields_str == NULL)
  {
    printf("-%s: cut: missing -f/--fields option\n", sysname); // hata mesajı
    return UNKNOWN;                                            // başarısız
  }

  // Field listesini parse et
  int fcount = 0;                                       // kaç field istendi
  int *fields = parse_fields_list(fields_str, &fcount); // listeyi dizi yap
  if (fields == NULL || fcount == 0)
  {
    printf("-%s: cut: invalid fields list\n", sysname); // hata mesajı
    free(fields);                                       // temizlik
    return UNKNOWN;                                     // başarısız
  }

  char *line = NULL; // getline buffer
  size_t cap = 0;    // buffer kapasitesi

  // stdin'den satır satır oku
  while (getline(&line, &cap, stdin) != -1)
  { // EOF olana kadar
    // Satır sonundaki \n varsa kaldır
    size_t len = strlen(line); // uzunluk
    if (len > 0 && line[len - 1] == '\n')
    {                       // \n var mı?
      line[len - 1] = '\0'; // kaldır
    }

    // strtok bozacağı için satırı kopyala
    char *copy = strdup(line); // satır kopyası
    if (copy == NULL)
      break; // kopya yoksa çık

    // delimiter string'i hazırla (strtok için)
    char dstr[2] = {delim, '\0'}; // ör: ":" veya "\t"

    // Önce token sayısını bul (kaç alan var?)
    int tok_count = 0;        // alan sayısı
    char *tmp = strdup(copy); // saymak için kopya
    if (tmp == NULL)
    {
      free(copy);
      break;
    } // kopya yoksa çık
    char *t = strtok(tmp, dstr); // ilk token
    while (t != NULL)
    {
      tok_count++;
      t = strtok(NULL, dstr);
    } // say
    free(tmp); // sayma kopyasını temizle

    // Token pointer dizisi oluştur
    char **tokens = NULL; // token dizisi
    if (tok_count > 0)
      tokens = malloc(sizeof(char *) * tok_count); // yer ayır
    int idx = 0;                                   // doldurma indexi

    // Asıl split: tokenları diziye koy
    t = strtok(copy, dstr); // split başlat
    while (t != NULL)
    {                         // token oldukça
      tokens[idx++] = t;      // pointer'ı sakla
      t = strtok(NULL, dstr); // sıradaki token
    }

    // İstenen field'ları sırayla bas (1-based)
    for (int k = 0; k < fcount; k++)
    {                       // her istenen field
      int want = fields[k]; // 1-based alan no
      const char *out = ""; // default boş

      if (want >= 1 && want <= tok_count)
      {                         // aralık kontrolü
        out = tokens[want - 1]; // 1-based -> 0-based
      }

      if (k > 0)
        putchar(delim);   // araya delimiter koy
      fputs(out, stdout); // alanı yaz
    }

    putchar('\n'); // satır sonu

    free(tokens); // token dizisini temizle
    free(copy);   // satır kopyasını temizle
  }

  free(fields);   // fields dizisini temizle
  free(line);     // getline buffer temizle
  return SUCCESS; // başarılı
}

// repeat builtin: komutu N kez çalıştırır
int run_repeat_builtin(struct command_t *command)
{
  // Kullanım: repeat N cmd args...
  if (command->args[1] == NULL || command->args[2] == NULL)
  {
    printf("-%s: repeat: usage: repeat N <command> [args...]\n", sysname); // kullanım mesajı
    return UNKNOWN;                                                        // hata
  }

  int n = atoi(command->args[1]); // N sayısını al
  if (n <= 0)
  {
    printf("-%s: repeat: N must be > 0\n", sysname); // N kontrolü
    return UNKNOWN;
  }

  // Çalıştırılacak komut adı: args[2]
  const char *cmd = command->args[2];

  // repeat için yeni argv oluştur: [cmd, args3..., NULL]
  // command->args şu an: [repeat, N, cmd, a1, a2, ..., NULL]
  char **new_argv = &command->args[2]; // cmd'den itibaren başlat

  // Komutun path'ini çöz
  char *resolved_path = resolve_executable_path(cmd); // PATH içinde bul
  if (resolved_path == NULL)
  {
    printf("-%s: %s: command not found\n", sysname, cmd); // bulunamadı
    return UNKNOWN;
  }

  // N kez çalıştır
  for (int i = 0; i < n; i++)
  {
    pid_t pid = fork(); // child oluştur
    if (pid == 0)
    {
      execv(resolved_path, new_argv);                         // komutu çalıştır
      printf("-%s: %s: %s\n", sysname, cmd, strerror(errno)); // execv hata
      exit(127);
    }
    waitpid(pid, NULL, 0); // her turda bitmesini bekle
  }

  free(resolved_path); // path'i temizle
  return SUCCESS;      // başarılı
}

// chatroom builtin: FIFO tabanlı basit chat
int run_chatroom_builtin(struct command_t *command)
{
  // Kullanım kontrolü: chatroom <roomname> <username>
  if (command->args[1] == NULL || command->args[2] == NULL)
  {
    printf("-%s: chatroom: usage: chatroom <roomname> <username>\n", sysname);
    return UNKNOWN;
  }

  const char *room = command->args[1]; // oda adı
  const char *user = command->args[2]; // kullanıcı adı

  // Oda klasörü yolu: /tmp/chatroom-<room>
  char room_dir[512];
  snprintf(room_dir, sizeof(room_dir), "/tmp/chatroom-%s", room);

  // Kullanıcının FIFO yolu: /tmp/chatroom-<room>/<username>
  char my_fifo[512];
  // my_fifo yolu buffer'a sığdı mı kontrol et
  int n1 = snprintf(my_fifo, sizeof(my_fifo), "%s/%s", room_dir, user); // yolu yaz
  if (n1 < 0 || n1 >= (int)sizeof(my_fifo))
  {                                                    // sığmadıysa
    printf("-%s: chatroom: path too long\n", sysname); // hata bas
    return UNKNOWN;                                    // çık
  }

  // 1) Oda klasörünü oluştur (varsa sorun değil)
  if (mkdir(room_dir, 0777) == -1)
  { // klasör oluşturmayı dene
    if (errno != EEXIST)
    { // zaten varsa sorun değil
      printf("-%s: chatroom: mkdir failed: %s\n", sysname, strerror(errno));
      return UNKNOWN;
    }
  }

  // 2) Kullanıcı FIFO'sunu oluştur (varsa sorun değil)
  if (mkfifo(my_fifo, 0666) == -1)
  { // fifo oluşturmayı dene
    if (errno != EEXIST)
    { // zaten varsa sorun değil
      printf("-%s: chatroom: mkfifo failed: %s\n", sysname, strerror(errno));
      return UNKNOWN;
    }
  }

  // 3) Reader child: kendi FIFO'muzdan gelen mesajları okuyup ekrana basacak
  pid_t reader_pid = fork(); // okuyucu process
  if (reader_pid < 0)
  {
    printf("-%s: chatroom: fork failed: %s\n", sysname, strerror(errno));
    return UNKNOWN;
  }

  if (reader_pid == 0)
  {
    // ===== CHILD (READER) =====

    // FIFO'yu O_RDWR açıyoruz ki open bloklamasın ve read beklesin
    int fd = open(my_fifo, O_RDWR); // hem okuma hem yazma aç
    if (fd < 0)
    {
      printf("-%s: chatroom: open fifo failed: %s\n", sysname, strerror(errno));
      exit(1);
    }

    char buf[1024]; // okuma bufferı
    while (1)
    {
      ssize_t n = read(fd, buf, sizeof(buf) - 1); // fifo'dan oku
      if (n > 0)
      {
        buf[n] = '\0';      // string sonu
        fputs(buf, stdout); // ekrana bas
        fflush(stdout);     // hemen göster
      }
      // n == 0 ise (EOF gibi) burada takılabilir; O_RDWR olduğundan genelde bekler
      // n < 0 ise hata olabilir, devam et
    }
  }

  // ===== PARENT (WRITER + INPUT LOOP) =====
  printf("Entered chatroom '%s' as '%s'. Type /exit to leave.\n", room, user);

  // Kullanıcıdan satır satır mesaj al
  char *line = NULL; // getline buffer
  size_t cap = 0;    // buffer kapasitesi

  while (1)
  {
    printf("chat> "); // chat prompt
    fflush(stdout);

    ssize_t r = getline(&line, &cap, stdin); // kullanıcıdan oku
    if (r == -1)
    { // Ctrl+D / EOF
      break;
    }

    // Satır sonundaki '\n' varsa temizle
    if (r > 0 && line[r - 1] == '\n')
    {
      line[r - 1] = '\0';
    }

    // Çıkış komutu
    if (strcmp(line, "/exit") == 0)
    {
      break;
    }

    // Gönderilecek mesaj formatı: "username: mesaj\n"
    char msg[1200];
    snprintf(msg, sizeof(msg), "%s: %s\n", user, line);

    // Oda klasörünü aç, içindeki diğer FIFO'lara mesaj gönder
    DIR *d = opendir(room_dir); // klasörü aç
    if (d == NULL)
    {
      printf("-%s: chatroom: opendir failed: %s\n", sysname, strerror(errno));
      continue;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    { // tüm dosyaları gez
      // "." ve ".." geç
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      {
        continue;
      }

      // Kendi FIFO'na mesaj göndermeyelim
      if (strcmp(ent->d_name, user) == 0)
      {
        continue;
      }

      // Hedef FIFO yolu
      char target_fifo[512];
      // target_fifo yolu buffer'a sığdı mı kontrol et
      int n2 = snprintf(target_fifo, sizeof(target_fifo), "%s/%s", room_dir, ent->d_name); // yolu yaz
      if (n2 < 0 || n2 >= (int)sizeof(target_fifo))
      {           // sığmadıysa
        continue; // bunu geç
      }

      // Her hedefe ayrı child ile yaz (ödev istiyordu)
      pid_t wp = fork(); // writer child
      if (wp == 0)
      {
        // Hedef FIFO'yu yazma modunda aç (non-blocking: karşı taraf yoksa takılmasın)
        int wfd = open(target_fifo, O_WRONLY | O_NONBLOCK);
        if (wfd >= 0)
        {
          write(wfd, msg, strlen(msg)); // mesajı yaz
          close(wfd);                   // kapat
        }
        exit(0); // writer child çık
      }
      // parent burada beklemiyor (çok child olursa zombie olabilir; basitçe ignore edebiliriz)
    }

    closedir(d); // klasörü kapat
  }

  free(line); // buffer temizle

  // Reader'ı durdur
  kill(reader_pid, SIGTERM);    // reader child'i öldür
  waitpid(reader_pid, NULL, 0); // reader bitmesini bekle

  // Çıkınca kendi FIFO'yu silmek istersen (temizlik)
  unlink(my_fifo); // kendi fifo dosyasını sil

  printf("Left chatroom '%s'.\n", room);
  return SUCCESS;
}

// Builtin komutları çalıştırır (child içinde veya normalde çağrılabilir)
// Başarılıysa SUCCESS, değilse UNKNOWN döner
int run_builtin_child(struct command_t *command)
{
  // Güvenlik: command veya name yoksa builtin yok
  if (command == NULL || command->name == NULL)
  {
    return UNKNOWN; // tanımsız
  }

  // help builtin: komut listesini basar
  if (strcmp(command->name, "help") == 0)
  {
    printf("Shell-ish builtins:\n");                            // başlık
    printf("  cd <dir>\n");                                     // cd
    printf("  exit\n");                                         // exit
    printf("  cut -d X -f list   (or --delimiter/--fields)\n"); // cut
    printf("  chatroom <room> <user>\n");                       // chatroom (sonra)
    printf("  help\n");                                         // help
    return SUCCESS;                                             // başarılı
  }

  // cut builtin: asıl cut fonksiyonunu çağır
  if (strcmp(command->name, "cut") == 0)
  {
    return run_cut_builtin(command); // cut'ı çalıştır
  }

  // repeat builtin: komutu N kez çalıştır
  if (strcmp(command->name, "repeat") == 0)
  {
    return run_repeat_builtin(command); // repeat'i çalıştır
  }

  // chatroom builtin daha sonra eklenecek
  return UNKNOWN; // bu isimde builtin yok
}

// repeat builtin: "repeat N cmd args..." komutunu N kez çalıştırır

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
  // help builtin: komut listesini bas (pipe olmadan da çalışsın)
  if (strcmp(command->name, "help") == 0)
  {
    return run_builtin_child(command); // help'i çalıştır
  }

  // cut builtin: stdin'den okuyup field'ları basar
  if (strcmp(command->name, "cut") == 0)
  {
    return run_cut_builtin(command); // cut fonksiyonunu çağır
  }

  // repeat builtin: komutu N kez çalıştır
  if (strcmp(command->name, "repeat") == 0)
  {
    return run_repeat_builtin(command); // repeat'i çalıştır
  }

  // chatroom builtin: oda chatine girer
  if (strcmp(command->name, "chatroom") == 0)
  {
    return run_chatroom_builtin(command); // chatroom'u çalıştır
  }

  // Eğer komut zinciri varsa (| kullanılmışsa), pipeline olarak çalıştır
  if (command->next != NULL)
  {
    return execute_pipeline(command); // pipe zincirini çalıştırıp çık
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
