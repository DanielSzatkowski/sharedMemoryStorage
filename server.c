#include <stdio.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>

/*zeby zadzialalo na linuxach - union semun jest problematyczne (niekeidy niezdefiniowane)*/
/* http://man7.org/tlpi/code/online/dist/svsem/semun.h.html */
#ifndef SEMUN_H
#define SEMUN_H

#include <sys/types.h>
#include <sys/sem.h>

#if ! defined(__FreeBSD__) && ! defined(__OpenBSD__) && \
                ! defined(__sgi) && ! defined(__APPLE__)

union semun {
    int                 val;
    struct semid_ds *   buf;
    unsigned short *    array;
#if defined(__linux__)
    struct seminfo *    __buf;
#endif
};

#endif

#endif

#define MSG_SIZE 64

 /** 1 = segment pamieci zapelniony  0 = segment pamieci pusty */

/********UZYWANE ZMIENNE GLOBALNE*******************/
struct data{
    uid_t   uid;            /*UID uzytkkownika*/
    short   isFull;          /*czy pamiec jest "pusta"? 0-pusta 1-zapelniona*/
    char    txt[MSG_SIZE];  /*wiadomosc*/
} *shared_data;

int     amountOfMessages;   /*bedzie przechowywac ilosc wiadomosci ktore pomiesci pamiec*/
struct  data *tab;          /*tablica przechowywujaca segmenty pamieci*/
key_t   shm_key;            /*klucz*/
int     shm_id;             /*identyfikator (deskryptor)*/
struct  shmid_ds buf;       /*bedzie przechowywal "statystyki pamieci wspoldzielonej"*/
short   flag = 0;           /*flaga sprawdzajaca czy jest jakas wiadomosc w pamieci - jesli nie flag = 0; jesli tak flag = 1*/
/*******************semafory************************/
key_t   sem_key;            /*klucz zbioru semaforow*/
int     sem_id;             /*id zbioru semaforow*/
struct  sembuf sops;
/****************************************************/

/** Funkcja wypisujaca komunikaty bledow i wychodzaca z programu **/
void error(char *txt)
{
    printf("\n");
    fprintf(stderr, txt, 0);
    printf("\n");
    exit(1);
}

/** Funkcja czyszczaca - jesli wystapil blad */
/** flag = 1 - usun zbior semaforow i usun pamiec wspoldzielona (flag != 1 - zajmij sie tylko semaforami)*/
/** txt - wiadomosc jaka przekazac funkcji error tak aby wyswietlila przyczyne zamkniecia programu */
void exitError(short flag, char *txt)
{
    /*zwolnienie pamieci zajmowanej przez wskaznik*/
    free(shared_data);

    /* usuwanie semaforow */
    if((semctl(sem_id, 0, IPC_RMID)) == -1){
        error("Nie udalo sie usunac semaforu");
    }


    if(flag == 1){
        /* usuwanie pamieci wspoldzielonej*/
        if((shmctl(shm_id, IPC_RMID, 0)) == 0){
            printf("Usuniecie: OK\n");
        } else {
            error("usuniecie: blad funkcji shmctl");
        }
    }

    error(txt);
}

/** i - numer semafora, op - operacja 1 (dodaj 1), -1(odejmij 1)*/
void semOperation(int i, int op)
{
    sops.sem_num = i;
    sops.sem_op = op;
    sops.sem_flg = 0;
    if((semop(sem_id, &sops, 1)) == -1){
        if(op > 0)
	    error("Nie udalo sie odblokowac semafora");
	else
		error("Nie udalo sie zablokowac semafora");
    }
}

/** Funkcja wyswietlajaca ksiege **/
void printBook()
{
    int i;
    struct passwd *user_struct;
    struct data *wsk;
    unsigned short semVals[amountOfMessages];
    union semun arg;
    arg.array = semVals;

    /*pobieram informacje o wartosci semaforow do tablicy semVals*/
    if(semctl(sem_id, 0, GETALL, arg) == -1){
		error("Blad w semctl - nie mozna odczytac wartosci semaforow");
	}

    /*sprawdz czy ksiega jest nadal pusta*/
    wsk = tab;
    if(flag == 0){
        for(i = 0; i < amountOfMessages; i++){
            if(semVals[i] == 1){            /*jesli segment pamieci nie jest zajety przez semafor*/
                /**blokuje i-ty semafor*/
                semOperation(i, -1);

                if(wsk -> isFull == 1){
                flag = 1;

                    semOperation(i, 1);
                    break;
                }

                /** odblokowuje i-ty semafor*/
                semOperation(i, 1);
            }

            wsk++;
        }
    }

    /* wyswietl zawartosc ksiegi */
    if(flag == 1){
        printf("\n______________  Ksiega skarg i wnioskow  ______________\n");

        wsk = tab;
        for(i = 0; i < amountOfMessages; i++){
            if(semVals[i] == 1){
                /** blokuje semafor */
                semOperation(i, -1);

                if(wsk -> isFull == 1){
                    if((user_struct = getpwuid(wsk -> uid)) == NULL){
                        error("Blad funkcji getpwuid");
                    }

                    printf("[%s]: ", user_struct -> pw_name);
                    printf("%s\n", wsk -> txt);
                }

                /** odblokowywuje semafor */
                semOperation(i, 1);
            }

            wsk++;
        }
    } else {
        printf("\nKsiega skarg i wnioskow jest jeszcze pusta\n");
    }

    return;
}
/************KONCZENIE PRACY SERWERA********************/
void endWork()
{
    printf("\nDostalem SIGINT => koncze i sprzatam... ");

    /* odlaczanie */
    if((shmdt(tab)) == -1){
        error("odlaczenie: blad funkcji shmdt");
    } else {
        printf("odlaczenie: OK ");
    }

    /* usuwanie pamieci wspoldzielonej*/
    if((shmctl(shm_id, IPC_RMID, 0)) == 0){
        printf("usuniecie: OK\n");
    } else {
        error("usuniecie: blad funkcji shmctl");
    }

    /* usuwanie semaforow */
    if((semctl(sem_id, 0, IPC_RMID)) == -1){
        error("Nie udalo sie usunac semaforu");
    }


	free(shared_data);

    exit(0);
}

void makeBlanks()
{
	int i;
    struct data *wsk;
    wsk = tab;

    for(i = 0; i < amountOfMessages; i++){
        /** blokuje semafor */
        semOperation(i, -1);

        wsk -> isFull = 0;
        wsk++;

        /** odblokowywuje semafor */
        semOperation(i, 1);
    }
}

int main(int argc, char *argv[])
{
	union semun arg; /*uzyty do funkcji semctl()*/

    if(argc != 3){
        error("Nieodpowiednia ilosc argumentow");
    }

    /****************************************************/
    /****************ROZPOCZECIE PRACY*******************/
    /****************************************************/

    /**Rzutowanie zmiennej przechowujaca ilsoc**/
    amountOfMessages = atoi(argv[2]);

    tab = (struct data*) malloc(amountOfMessages*sizeof(struct data));

    /***************OBSLUGA SYGNALOW*********************/
    signal(SIGINT, endWork);
    signal(SIGTSTP, printBook);

    /******WYPISANIE KOMUNIKATU O ROZPOCZECIU PRACY******/
    printf("[SERWER]: Ksiega skarg i wnioskow (wariant A)\n");

    /*********************KLUCZ SEM**************************/
    if((sem_key = ftok(argv[1], 1)) == -1){
        error("Nie udalo sie utworzyc klucza - semafor");
    }

    /*********************KLUCZ SHM**************************/
    printf("[SERWER]: Tworze klucz... ");
    if((shm_key = ftok(argv[1], 2)) == -1){
        error("Nie udalo sie utworzyc klucza - pamiec wspoldzielona");
    } else {
        printf("OK(klucz: %ld)\n", (long int)shm_key);
    }

    /*****TWORZENIE ZBIORU SEMAFOROW***************/
    if((sem_id = semget(sem_key, amountOfMessages, 0666 | IPC_CREAT)) == -1){
        error("Nie udalo sie utworzyc zbioru semaforow");
    }

    /**ustawiam semafoty jako otwarte*/
	int j;
	arg.val = 1;
	for(j = 0; j < amountOfMessages; j++){
   		if(semctl(sem_id, j, SETVAL, arg) == -1){
    			error("Blad funkcji semctl()");
    		}
	}

    /*****TWORZENIE PAMIECI WSPOLDZIELONEJ***************/
    printf("[SERWER]: Tworze segment pamieci wspoldzielonej, liczba wpisow: %d... ", amountOfMessages);
    if((shm_id = shmget(shm_key, sizeof(tab)*amountOfMessages, 0666 | IPC_CREAT)) == -1){
        exitError(0, "Nie udalo sie utworzyc pamieci wspoldzielonej");
    } else {
        /*uzyskanie informacji o naszym segmencie pamieci (m.in. o rozmiarze)*/
        if((shmctl(shm_id, IPC_STAT, &buf)) == -1){
            exitError(1, "Blad funkcji shmctl - nie udalo sie uzyskac informacji o segmancie pamieci");
        }

        printf("OK(id: %d, rozmiar: %zuB)\n", shm_id, buf.shm_segsz);
    }

    /***********DOLACZANIE PAMIECI WSPOLNEJ**************/
    printf("[SERWER]: Dolaczam pamiec wspoldzielona... ");
    if((tab = shmat(shm_id, NULL, 0)) == (struct data *)-1){
        exitError(1, "Nie udalo sie podlaczyc pamieci wspoldzielonej");
    } else {
        printf("OK(adres: %ld)\n", (long int)tab);
    }

    /**USTAWIENIE WIADOMOSCI W PAMIECI WSPOLDZIELONEJ JAKO PUSTEJ***/
    makeBlanks();

    /***************POINFORMOWANIE UZYTKOWNIKA O DOSTEPNEJ*****
    ****************OPCJI SPRAWDZENIA WIADOMOSCI W KSIEDZE****/
    printf("[SERWER]: Nacisnij Crtl^Z by wyswietlic stan ksiegi\n");

    /******************SERWER OCZEKUJE NA SYGNALY*************/
	while(1){
    		sleep(1);
	}

    return 0;
}
