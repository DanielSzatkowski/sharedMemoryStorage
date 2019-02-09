#define _WITH_GETLINE
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define MSG_SIZE 64

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

/********UZYWANE ZMIENNE GLOBALNE*******************/
struct data{
    uid_t   uid;                        /*UID uzytkkownika*/
    short   isFull;                      /*czy paamiec jest "pusta"? 0-pusta 1-zapelniona*/
    char    txt[MSG_SIZE];              /*wiadomosc*/
} *shared_data;
struct 	    shmid_ds buf;               /*bedzie przechowywal "statystyki pamieci wspoldzielonej"*/
int 	    amountOfMessages = 0;       /*ile zmiennych moze pomiescic tablica*/
struct 	    data *tab;                  /*tablica przechowywujaca segmenty pamieci*/
int	        elementsInMem = 0;          /*ilosc zajetych miejsc*/
int 	    ind;                        /*nr indeksu tablicy gdzie mozna dodac element*/
uid_t 	    userUID;                    /*UID osoby wysylajacej wiadomosc*/
key_t 	    shm_key;
int 	    shm_id;

/****************SEMAFORY***************************/
key_t           sem_key;                /*klucz zbioru semaforow*/
int             sem_id;                 /*id zbioru semaforow*/
struct sembuf   sops;                   /*uzywana do przeprowadzania operacji na semaforach*/

struct semid_ds semStat;                /*zmienna ktora bedzie przechowywac informacje na temat zbioru semaforow*/
int             amountOfSem;            /*ilosc semaforow w zbiorze*/
union           semun arg;	            /*do semctl()*/
/***************************************************/

/** Funkcja kontroli bledow **/
void error(char *txt)
{
    printf("\n");
    fprintf(stderr, txt, 0);
    printf("\n");
    exit(1);
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

int main(int argc, char *argv[])
{
    if(argc != 2){
        error("Nieodpowiednia ilosc argumentow");
    }

    /****************************************************/
    /****************ROZPOCZECIE PRACY*******************/
    /****************************************************/
    char *buff = NULL;              /*zmienne ktore przydadza sie przy wywolaniu getline() pozniej*/
    size_t buffsize = MSG_SIZE;
    int i;
    struct data *wsk;

    printf("Klient ksiegi skarg i wnioskow wita!\n");

    /*********************KLUCZ SEM**************************/
    if((sem_key = ftok(argv[1], 1)) == -1){
        error("Nie udalo sie utworzyc klucza - semafor");
    }

    /*************TWORZENIE KLUCZA SHM*******************/
    printf("[KLIENT]: Tworze klucz... ");
    if((shm_key = ftok(argv[1], 2)) == -1){
        error("Nie udalo sie utworzyc klucza");
    } else {
        printf("OK(klucz: %ld)\n", (long int)shm_key);
    }

    /**OTWIERANIE ZBIORU SEMAFOROW I ZEBRANIE INFORMACJI*/
    if((sem_id = semget(sem_key, amountOfMessages, 0)) == -1){
        error("Nie udalo sie otworzyc zbioru semaforow");
    } else {
        /*uzyskanie informacji o semaforach*/
	    arg.buf = &semStat;
        if((semctl(sem_id, 0, IPC_STAT, arg)) == -1){
            error("Blad funkcji semctl");
        } else {
            amountOfSem = semStat.sem_nsems;
        }
    }

    /******OTWIERANEI PAMIECI WSPOLNEJ***************/
    printf("[KLIENT]: Otwieram segment pamieci wspoldzielonej... ");
    if((shm_id = shmget(shm_key, 0, 0)) == -1){
        error("Nie udalo sie uzyskac shm_id");
    } else {
        /*uzyskanie informacji o naszym segmencie pamieci (m.in. o rozmiarze)*/
        if((shmctl(shm_id, IPC_STAT, &buf)) == -1){
            error("Blad funkcji shmctl");
        }

        printf("OK(id: %d, rozmiar: %zuB)\n", shm_id, buf.shm_segsz);

        /* obliczam maksymalna ilosc elementow jaka moze pomiescic pamiec wspoldzielona*/
        amountOfMessages = buf.shm_segsz/sizeof (struct data*);
    }

    /***********"PODLACZANIE SIE" DO PAMIECI WSPOLNEJ**************/
    tab = (struct data*) malloc(amountOfMessages*sizeof(struct data));
    printf("[KLIENT]: Podlaczam sie do pamieci wspoldzielonej... ");
    if((tab = shmat(shm_id, NULL, 0)) == (struct data *)-1){
        error("Nie udalo sie podlaczyc pamieci wspoldzielonej");
    } else {
        printf("OK(adres: %ld)\n", (long int)tab);
    }

    /************PRZESYLANIE WIADOMOSCI****************************/
    /*sprawdz czy jest jeszcze puste miejsce jesli nie to wyjdz*/
    wsk = tab;
    ind = -1;

	/**zwroc wartosci semaforow*/
    unsigned short semVals[amountOfSem];
	arg.array = semVals;

	if(semctl(sem_id, 0, GETALL, arg) == -1){
	    /*gdyby wystapil blad - sprzatam po sobie*/
	    printf("Wystapil blad - odlaczam sie od pamieci wspoldzielonej... ");
    	if((shmdt(tab)) == -1){
       		 error("odlaczenie: blad funkcji shmdt");
    	} else {
        	printf("odlaczenie: OK\n");
    	}

        free(shared_data);

		error("Blad w semctl - nie mozna odczytac wartosci semaforow");
	}

    for(i = 0; i < amountOfMessages; i++){
        if(semVals[i] == 1){ /*jesli semafor jest usatwiony na jeden - czyli wolny*/

            /*blokuje semafor*/
        	semOperation(i, -1);

        	if((wsk -> isFull) == 1){  /*jesli pamiec jest zajeta*/
           	elementsInMem++;

        	} else {                /*jesi pamiec jest pusta (jako pierwsza) to zapisz indeks zeby moc pozniej to zapisac*/
           	 if(ind == -1){
                	ind = i;
            	}
        	}

        	/*odblokowywuje semafor*/
        	semOperation(i, 1);
        }

        wsk++;
    }

    /*jesli nie ma miejsca - wyjdz*/
    if(ind == -1){
	    printf("[KLIENT]: Nie ma juz miejsca!\n");

	/*********ODLACZAM SIE OD PAMIECI WSPOLNEJ********************/
    	printf("[KLIENT]: Odlaczam sie od pamieci wspoldzielonej... ");
    	if((shmdt(tab)) == -1){
       		 error("odlaczenie: blad funkcji shmdt");
    	} else {
        	printf("odlaczenie: OK\n");
    	}

	exit(0);
    }

    /*wypisz ile jest pustego miejsca*/
    printf("[Wolne miejsca: %d(na %d)]\n", amountOfMessages-elementsInMem, amountOfMessages);

    /*"co chcesz przeslac"*/
	/*blokuje semafor*/
    semOperation(ind, -1);

    printf("[KLIENT]: Co chcesz umiescic w ksiedze?\n");
    getline(&buff, &buffsize, stdin);

    /*stworz strukture*/
	shared_data = (struct data*) malloc(sizeof(struct data));
    shared_data -> isFull = 1;

	buff[strlen(buff)-1] = '\0';     /* usuwam koniec linii */
	strcpy(shared_data->txt, buff);

	userUID = getuid();
	shared_data -> uid = userUID;

    /***********"wyslij"***************************/
    tab[ind] = *shared_data;

    /*odblokowywuje semafor*/
    semOperation(ind, 1);

    /*********************komunikat****************/
    printf("[KLIENT]: Wpisalem komunikat do pamieci wspoldzielonej\n");
    printf("[KLIENT]: Dziekuje za dokonanie wpisu do ksiegi!\n");

    /*********ODLACZAM SIE OD PAMIECI WSPOLNEJ********************/
    printf("[KLIENT]: Odlaczam sie od pamieci wspoldzielonej... ");
    if((shmdt(tab)) == -1){
        error("odlaczenie: blad funkcji shmdt");
    } else {
        printf("odlaczenie: OK\n");
    }

	free(shared_data);

    return 0;
}
