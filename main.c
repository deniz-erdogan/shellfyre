/*
 * Deniz Erdogan 69572
 * Oya Suran 69337
 */

#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>


const char *sysname = "shellfyre";

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
        printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
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
    command->name = (char *)malloc(strlen(pch) + 1);
    if (pch == NULL)
        command->name[0] = 0;
    else
        strcpy(command->name, pch);

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
            continue;                                        // empty arg, go for next
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
            struct command_t *c = malloc(sizeof(struct command_t));
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
            command->redirects[redirect_index] = malloc(len);
            strcpy(command->redirects[redirect_index], arg + 1);
            continue;
        }

        // normal arguments
        if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
        {
            arg[--len] = 0;
            arg++;
        }
        command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
        command->args[arg_index] = (char *)malloc(len + 1);
        strcpy(command->args[arg_index++], arg);
    }
    command->arg_count = arg_index;
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
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    // FIXME: backspace is applied before printing chars
    show_prompt();
    int multicode_state = 0;
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
        if (c == 27 && multicode_state == 0) // handle multi-code keys
        {
            multicode_state = 1;
            continue;
        }
        if (c == 91 && multicode_state == 1)
        {
            multicode_state = 2;
            continue;
        }
        if (c == 65 && multicode_state == 2) // up arrow
        {
            int i;
            while (index > 0)
            {
                prompt_backspace();
                index--;
            }
            for (i = 0; oldbuf[i]; ++i)
            {
                putchar(oldbuf[i]);
                buf[i] = oldbuf[i];
            }
            index = i;
            continue;
        }
        else
            multicode_state = 0;

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
    buf[index++] = 0; // null terminate string

    strcpy(oldbuf, buf);

    parse_command(buf, command);

    // print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
    return SUCCESS;
}

int process_command(struct command_t *command);
int isBackground(struct command_t *command);
int filesearch_helper(char *directory_name, char* substr, bool o_flag, bool r_flag);

int isBackground(struct command_t *command) {
    for (int i = command->arg_count; i > 0; --i) {

        printf("%s \n", command->args[i]);
//        if (strcmp(command->args[i], "&") == 0) {
//            return 1;
//        }
    }
    return 0;
}

/*
 * Helper function to search for files recursively
 */
int filesearch_helper(char *directory_name, char *substr, bool o_flag, bool r_flag){

    DIR *d;
    // Open the given directory
    struct dirent *dir;
    d = opendir(directory_name);
    printf("D value when opening %s is %d \n", directory_name, d);

    if (d)
    {
        printf("====== OPENED: %s ========\n", directory_name);

        // Start reading the contents of the directory
        while ((dir = readdir(d)) != NULL)
        {
            char *ptr = strstr(dir->d_name, substr);

            if (ptr != NULL) // A match
            {
                printf("'%s' contains '%s'\n", dir->d_name, substr);

                if (o_flag)
                {
                    char sys_command[50];
                    strcpy(sys_command, "xdg-open ");
                    strcat(sys_command, ptr);
                    system(sys_command);
                }

            }

            // If it is a directory, make a recursive call in there
            if (dir->d_type == DT_DIR && r_flag && (strcmp(dir->d_name, ".") != 0) && (strcmp(dir->d_name, "..") != 0)) {
                filesearch_helper(dir->d_name, substr, o_flag, r_flag);
            }


        }
        closedir(d);
        printf("====== CLOSED: %s ========\n", directory_name);

    }

}

// ref: https://www.geeksforgeeks.org/implement-your-own-tail-read-last-n-lines-of-a-huge-file/
/*
 * Prints the last n (maximum 10) directories in the history of the user.
 */
void print_history(FILE* in, int n)
{
    int count = 0;
    long pos;
    char str[300];

    // Reach the end of the file
    if (fseek(in, 0, SEEK_END)) {
        perror("fseek() failed");
    }
    else
    {
        pos = ftell(in);
        // Climb up n lines
        while (pos)
        {
            if (!fseek(in, --pos, SEEK_SET))
            {
                if (fgetc(in) == '\n')
                    if (count++ == n)
                        break;
            }
            else
                perror("fseek() failed");
        }

        printf("Printing last %d lines -\n", n);
        char letter = 97 + n-1;
        int num = n;
        while (fgets(str, sizeof(str), in)) {
            printf("%c %d) %s", letter, num, str);
            letter--;
            num--;
        }

        char c;
        printf("\n Enter something:");
        printf("\n Assuming you entered 2....\n");


        // Again, finding the corresponding lines by climbing
        // the necessary amount upwards.
        count = 0;
        if (fseek(in, 0, SEEK_END))
            perror("fseek() failed");
        else {
            pos = ftell(in);
            while (pos) {
                if (!fseek(in, --pos, SEEK_SET)) {
                    if (fgetc(in) == '\n')
                        if (count++ == 2)
                            break;
                } else
                    perror("fseek() failed");
            }
            fgets(str, sizeof(str), in);
            str[strlen(str)-1]='\0';

            // Change into desired directory
            if (chdir(str) != 0){
                printf ("chdir failed - %s\n", strerror (errno));
            }

        }
    }
    printf("\n\n");
}

int my_min(int first, int second) {
    return ((first > second) ? second : first);
}

int loaded = 0;
int main()
{
    while (1)
    {
        struct command_t *command = malloc(sizeof(struct command_t));
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

int process_command(struct command_t *command) {
    int r;
    if (strcmp(command->name, "") == 0)
        return SUCCESS;

    if (strcmp(command->name, "exit") == 0){
        if (loaded)
        {
            system("sudo rmmod my_module.ko");
            printf("Previously installed module has been removed.\n");
        }

        return EXIT;
    }

    if (strcmp(command->name, "cd") == 0) {
        if (command->arg_count > 0) {
            r = chdir(command->args[0]);
            if (r == -1) {
                printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
            } else {
                // Write the directory into the history file.
                FILE *fp = fopen("/home/cdh_history.txt", "a");
                char cwd[256];
                getcwd(cwd, sizeof(cwd));
                printf("\n%s\n", cwd);
                strcat(cwd, "\n");
                fprintf(fp,cwd);
                fclose(fp);
            }
            return SUCCESS;
        }
    }

    if (strcmp(command->name, "take") == 0) {
        printf("Creating Directories \n");

        // parsing
        char delim[] = "/";
        char *ptr = strtok(command->args[0], delim);
        while (ptr != NULL) {
            // Create first directory
            int check = mkdir(ptr, 0777);

            if (!check) {
                r = chdir(ptr);
                if (r == -1) {
                    printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
                }
            } else { // This most likely means that the directory already existed.
                r = chdir(ptr);
                if (r == -1) {
                    printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
                }
            }
            ptr = strtok(NULL, delim);
        }
        return SUCCESS;
    }

    // Start searching with the proper flags
    if (strcmp(command->name, "filesearch") == 0) {
        bool o_flag = false;
        bool r_flag = false;

        if (command->arg_count == 2) {
            if (strcmp(command->args[1], "-o") == 0) o_flag = true;
            if (strcmp(command->args[1], "-r") == 0) r_flag = true;
        }

        if (command->arg_count == 3) {
            if (strcmp(command->args[2], "-o") == 0) o_flag = true;
            if (strcmp(command->args[2], "-r") == 0) r_flag = true;
        }

        // Start searching on the current folder
        filesearch_helper(".", command->args[0], o_flag, r_flag);

    }

    if (strcmp(command->name, "courseprep") == 0) {

        // Create main course directory
        int check = mkdir(command->args[0], 0777);
        if (check) {
            printf("Could not create %s directory. \n", command->args[0]);
        }
        // Create HW, Lecture Notes, Syllabus, Projects, PastExams
        r = chdir(command->args[0]);
        if (r == -1) {
            printf("Could not cd into %s. \n", command->args[0]);
        }

        check = mkdir("HW", 0777);
        if (check) {
            printf("Could not create HW directory. \n");
        }

        check = mkdir("LectureNotes", 0777);
        if (check) {
            printf("Could not create LectureNotes directory. \n");
        }
        chdir("LectureNotes");
        FILE *fp;
        fp = fopen("NOTE1.txt", "w");

        time_t t;   // not a primitive datatype
        time(&t);

        // Create a note file with the days date
        fprintf(fp, "The First Note for the %s course has been taken at: %s", command->args[0], ctime(&t));
        fclose(fp);
        chdir("../");

        check = mkdir("Projects", 0777);
        if (check) {
            printf("Could not create Projects directory. \n");
        }

        check = mkdir("Syllabus", 0777);
        if (check) {
            printf("Could not create Syllabus directory. \n");
        }

        check = mkdir("PastExams", 0777);
        if (check) {
            printf("Could not create PastExams directory. \n");
        }

        chdir("../");

    }

    // Concatenate the args and makes Didem Unat say them along
    // with an ASCII portrait.
    if (strcmp(command->name, "didemunatsays") == 0) {
        char says[300];
        strcpy(says, "\nDidem Unat says: ");
        for (int i = 0; i < command->arg_count; ++i) {
            strcat(says, command->args[i]);
            strcat(says, " ");
        }

        char *didem_hoca = "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!7777777!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!7JY5PGGGGGGGGP5J?7!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!!!!!!!!??!!77!?JJ5B#&&&&##&&&&&&&&##BP5YJ77!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!!!!!!!75BP555G#&&&@@@&&&&@@@&&&&&&&&&&#BBGGPY7!!!!!!!!!!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!!!!!!7?YB&&&&&@@&@@&&&&&@@@&&&&&&&&&&&&&####&G57!!!!!!!!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!!!!!7P#&&@@@&&&&@@@@@@&&&&@@&@@@@@@&&&@@@&&&&&B5?7!!!!!!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!!!!!YG&&@@@#&&@@@@@@@&@@&&&&&&###&&&@@@@@@@@&&&&#B5!!!!!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!!!7Y#&&&&&@@@@@@@@&&##PJ7777?J777?J5G#&@@@@@&&&&@@&BPJ7!!!!!!!!!!!!!!\n"
                           "!!!!!!!!!!!?5B&@@&&&@@&@@@&&&GPJ!~~^^^^^^^^~~~!7?5B&@@@&&&&@@@@&#BJ!!!!!!!!!!!!!\n"
                           "!!!!!!!!!7Y5#&@@@@@@@@&@&BB#B7~^^^^::::^^^^^^^~~~!?5B&@@&@@@@@@@&B?!!!!!!!!!!!!!\n"
                           "!!!!!!!!J5GGB&&@@&@@@@@&GPB#5~~^^::::::::^^^^^^~~!77JB&@@@@@@@@@&&#J!!!!!!!!!!!!\n"
                           "!!!!!!7YGGGG#@@@@@@@@@@#P5Y7~~^^^:::::::::^^^^^~~!!77Y#&@@@@@@@@&@#?!!!!!!!!!!!!\n"
                           "!!!!!5GBGPB&@@@@@@@@@@&BY7~~~^^::::::::::::^^^^^^~~!7?Y#@@@@@@@@@B7~!!!!!!!!!!!!\n"
                           "!!!!YG5Y5B&&@@@@@@@@@&BY??J?7777!~^^::::::^^~~~~~!!!77J5&@@@@@@@@J!~~~~!!!!!!!!!\n"
                           "!!!!P5?JY#@@@@@@@@@@@#GGBB55YJY555J7!~^^^~7?JY55P55PGGPYG@@@@@@@@@&#BG5!!!!!!!!!\n"
                           "!!!7B5PB#&&&@@&&&@@@&&&55YYPPP55YYJPP7~^!?5YJJYY5YYYYGB#&&@@@@@@@@@@@@&Y~!!!!!!!\n"
                           "!!!J5G@@@@@@@@@&@@@@&BGYYG#&#&&#5P55G##B@&G55PG&&&#&G5Y5#&@@@@@@@@@@&#&J~!!!!!!!\n"
                           "!!!7?G&@@@@@@@@@&@@@@BBYJJJJJJ?7777!5B!~G#?7777?YY5P5J?PG#@@@@&&&&@@&&G!!!!!!!!!\n"
                           "!!!!!?B#&&&@@@@@@@@@@G5PJ7!~~~~~~^~YG7^^!BY~~~~!!!!~!75BP&@@@@@@&&&@@&&P!!!!!!!!\n"
                           "!!!!!7P&&&@@@@@@@@@@@BP5PJ7~^^^^~?P5?!^^~7557~::^^~~7YP5G&@@@&@@@&@@@@@&G!!!!!!!\n"
                           "!!!!!!JG####&@@@@@@@&#BPYYJ????JYYJ77~::^!?JP5??????JY5P#&&&#&@@@@@@@@@&#J~!!!!!\n"
                           "!!!!!!?5GP##@@@@@&####BG5J?!!~!7?????!^^~7??J??7!!77?Y5G#&&B&@@@&@@@@@@@#Y!!!!!!\n"
                           "!!!!!!!!7YG&@@@@@&5JJGGG5YJ77?YJ775BBBGGGBBGJ!7?7!77JY5G#BGB@@@@#B#&&@&&#7!!!!!!\n"
                           "!!!!!!!!!!7#@@&@@@#7??5P55J?JJ?7!!!!7J555J7!!!!7J??JYY5GG5B@@&#@&B#BB#BGJ!!!!!!!\n"
                           "!!!!!!!!!!~J&@&&@@@&BP&#5YJ777JPBG555YJJJ5Y55GGY?77JJ?PBPB@@@&&&#GYJ?7!!~!!!!!!~\n"
                           "!!!!!!!!!!!~!5B#&@@@&B&@#YJ?!!!7PGJ7~^^^:~~75BY7777??Y#@@@@@#GBGY7~~~~~~!!!!!!~~\n"
                           "!!!!!!!!!!!!~~~!?5P55YP#@BYJ7!!!?JYJ?!!7!7YYY?777!7?Y#@@@@&B7~!~~!!!!!!!!!!!!~~~\n"
                           "!!!!!!!!!!!!!!!~~~!!!!!?5BB5Y7!7!7?JJJJJJYYJ??7!!7JYB@@#PY?!~~~!!~!!!!!!!~~~~~~~\n"
                           "!!!!!!!!!!!!!!!!!!!!!!!!7YBBG5?7!~~~~~~~~~!!!!!!?Y5#G?!~~~~~~!~~~~~~~~~~~~~~~~~~\n"
                           "!!!!!!!!!!!!!!!!!!!!!!!~!5GBBBG5?7~~^^^^^^~~7?J5PPG5J~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
                           "!!!!!!!!!!!!!!!!!!!!!~~?PG55GGBBGPYJ?7777?JY5PGGGBBP!~!!~!~~~~~~~~~~~~~~~~!~~~~~\n"
                           "!!!!!!!!~~~~~~!!!!!!~!YGP5YJYPPGGBBBGGPPPGGGGGGGBBB#P7~!!!!!!!!~~~~~~~~~~~~~~~~~\n"
                           "!!!~~~!~~~~~~~~~~!!~!5PPYJJ??J5PPGGBBBBBBBBBGGGGGPGGBGY!!~~!!!!~~~~~~~~~~~~~~~~~\n"
                           "!!~~~~~~~~~~~!!77J~.?PP5J??7?77J5555PGGGGGGGPP55555PGGG!^7!~~~~~~~~~~~~~~~~~~~~~\n"
                           "~~~~~~~!!!7?JYYY5J..55P5?J?7?7!!7JYYYYY55555YYYYJYJPPPP!.J5J7!~~~~~~~~~~~~~~~~~~\n"
                           "~~!!!7?JYYYYYYYYY~ ^5555JJ?!777!!7777???J????YYJJJJPPPP~.7555Y?77!!!~~~~~~~~~~~~\n";

        printf("%s\n", says);
        printf("%s", didem_hoca);
    }

    if (strcmp(command->name, "joker") == 0) {

        // create cronjob file and fill it with time schedule
        char cron[300]; // create crontab directory file
        strcpy(cron, "*/1 * * * *  XDG_RUNTIME_DIR=/run/user/$(id -u) notify-send \"$(curl https://icanhazdadjoke.com/)\"");

        // create file path to add cronjob list.
        char filepath[50];
        strcpy(filepath, "/home/mycronfile.txt");
        FILE *job_file = fopen(filepath, "w");

        fprintf(job_file, "%s\n", cron);
        fclose(job_file);

        system("crontab /home/mycronfile.txt");
    }


    if (strcmp(command->name, "cdh") == 0) {
        char *c;
        int ctr = 1;

        FILE *fptr = fopen("/home/cdh_history.txt", "r");


        // By counting the new line characters, get the line count in the file
        for (c = getc(fptr); c != EOF; c = getc(fptr))
            if (c == '\n') // Increment count if this character is newline
                ctr = ctr + 1;
        fclose(fptr);
        printf(" The lines in the file are : %d \n \n", ctr);

        // If there are no files inside the list, issue a warning
        if (!ctr) {
            printf("\n\nWARNING! Not enough directories in the history.\n\n");
        } else {
            fptr = fopen("/home/cdh_history.txt", "r");
            print_history(fptr, my_min(ctr, 10));
        }
    }


    if (strcmp(command->name, "pstraverse") == 0)
    {
        if (!loaded)
        {
            system("sudo insmod my_module.ko");
            printf("Module has been loaded.\n");
            loaded = 1;

        } else{
            printf("Module already loaded!\n");
        }


    }



    pid_t pid = fork();

    if (pid == 0) // child
    {
        // increase args size by 2
        command->args = (char **) realloc(
                command->args, sizeof(char *) * (command->arg_count += 2));

        // shift everything forward by 1
        for (int i = command->arg_count - 2; i > 0; --i)
            command->args[i] = command->args[i - 1];
        // set args[0] as a copy of name
        command->args[0] = strdup(command->name);
        // set args[arg_count-1] (last) to NULL
        command->args[command->arg_count - 1] = NULL;

        // Execute shell commans in the bin directory
        char command_base[20];
        strcpy(command_base, "/bin/");
        strcat(command_base, command->name);
        execv(command_base, command->args);
        exit(0);
    } else {

        // Waiting is applied in accordance with the given
        // arguments
        if (command->background) {
            return SUCCESS;
        } else {
            wait(NULL);
            return SUCCESS;
        }


    }

    printf("-%s: %s: command not found\n", sysname, command->name);
    return UNKNOWN;
}



