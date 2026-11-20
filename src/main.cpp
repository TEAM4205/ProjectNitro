#include "main.h"
#include <algorithm>
#include "nitrogen/Storage.hpp"
#include "nitrogen/recordnreplay/replayer.hpp"
#include "nitrogen/recordnreplay/rnr.hpp"
#include "nitrogen/recordnreplay/buttonlog.hpp"
#include "nitrogen/neobjects/neomotor.h"
#include "nitrogen/elementals/native_menu_mgr.hpp"
#include "pros/motors.h"

NeoMotor left_mg("left", {-6, -7, -8});
NeoMotor right_mg("right", {9, 10, 11});

NeoMotor intake("intake", {14, 15});
pros::Motor intakeGetter(14);
int runid;

// Trim por lado: offset (+/-) que se suma a la MAGNITUD de la potencia, para compensar un robot que tira mas de un lado.
// Un comando 0 sigue siendo 0. Vive en RAM: se pierde al reiniciar el programa.
int trimLeft = 0, trimRight = 0;

int trimmed(int v, int trim) {
	if (v == 0) return 0;
	int m = std::clamp(abs(v) + trim, 0, 127);
	return v > 0 ? m : -m;
}

// left / right: -127..127, cada lado por separado (con el trim aplicado)
void moveTank(int left, int right) {
	left_mg.move(trimmed(left, trimLeft));
	right_mg.move(trimmed(right, trimRight));
}

// ---- Cheesy drive (entradas y salidas en -1..1) ----
constexpr double CD_PI = 3.14159265358979;
constexpr double CD_TURN_NONLINEARITY = 0.65;
constexpr double CD_NEG_INERTIA_SCALAR = 4.0;
constexpr double CD_SENSITIVITY = 1.0;
constexpr double DRIVE_DEADBAND = 0.05;
constexpr double DRIVE_SLEW = 0.1; // por iteracion (10 ms)

// Curva senoidal aplicada dos veces: mas control fino con el stick cerca de 0
static double cdTurnRemapping(double iturn) {
	double denominator = sin(CD_PI / 2 * CD_TURN_NONLINEARITY);
	double first = sin(CD_PI / 2 * CD_TURN_NONLINEARITY * iturn) / denominator;
	return sin(CD_PI / 2 * CD_TURN_NONLINEARITY * first) / denominator;
}

double quickStopAccumlator = 0.0;
double negInertiaAccumlator = 0.0;
static void cdUpdateAccumulators() {
	if (negInertiaAccumlator > 1) negInertiaAccumlator -= 1;
	else if (negInertiaAccumlator < -1) negInertiaAccumlator += 1;
	else negInertiaAccumlator = 0;

	if (quickStopAccumlator > 1) quickStopAccumlator -= 1;
	else if (quickStopAccumlator < -1) quickStopAccumlator += 1;
	else quickStopAccumlator = 0.0;
}

double cdPrevTurn = 0.0;
double cdPrevThrottle = 0.0;
std::pair<double, double> cheesyDrive(double ithrottle, double iturn) {
	bool turnInPlace = false;
	double linearCmd = ithrottle;
	if (fabs(ithrottle) < DRIVE_DEADBAND && fabs(iturn) > DRIVE_DEADBAND) {
		linearCmd = 0.0;
		turnInPlace = true;
	} else if (ithrottle - cdPrevThrottle > DRIVE_SLEW) {
		linearCmd = cdPrevThrottle + DRIVE_SLEW;
	} else if (ithrottle - cdPrevThrottle < -(DRIVE_SLEW * 2)) {
		linearCmd = cdPrevThrottle - (DRIVE_SLEW * 2); // el doble de slew al frenar
	}

	double remappedTurn = cdTurnRemapping(iturn);

	double left, right;
	if (turnInPlace) {
		left = remappedTurn * fabs(remappedTurn);
		right = -remappedTurn * fabs(remappedTurn);
	} else {
		negInertiaAccumlator += (iturn - cdPrevTurn) * CD_NEG_INERTIA_SCALAR;
		double angularCmd = fabs(linearCmd) * (remappedTurn + negInertiaAccumlator) * CD_SENSITIVITY - quickStopAccumlator;
		right = left = linearCmd;
		left += angularCmd;
		right -= angularCmd;
		cdUpdateAccumulators();
	}

	cdPrevTurn = iturn;
	cdPrevThrottle = ithrottle;
	return std::make_pair(std::clamp(left, -1.0, 1.0), std::clamp(right, -1.0, 1.0));
}

// El trim vive en la SD (/usd/trim.txt: "izq der") para sobrevivir reinicios; sin SD o sin archivo queda en 0
void loadTrim() {
	trimLeft = trimRight = 0;
	if (FILE* f = fopen("/usd/trim.txt", "r")) {
		int l, r;
		if (fscanf(f, "%d %d", &l, &r) == 2) {
			trimLeft = std::clamp(l, -127, 127);
			trimRight = std::clamp(r, -127, 127);
		}
		fclose(f);
	}
}

void saveTrim() {
	if (FILE* f = fopen("/usd/trim.txt", "w")) {
		fprintf(f, "%d %d\n", trimLeft, trimRight);
		fclose(f);
	}
}

// Modo trim (X+Y+L1+R1): up/down ajustan el derecho, left/right el izquierdo (+1 / -1, repite al mantener).
// X prueba 1000 ms a maxima con el trim puesto; A acepta y vuelve al opcontrol normal.
void trimMode(pros::Controller& master) {
	const pros::controller_digital_e_t btn[4] = {DIGITAL_UP, DIGITAL_DOWN, DIGITAL_RIGHT, DIGITAL_LEFT};
	int* const target[4] = {&trimRight, &trimRight, &trimLeft, &trimLeft};
	const int delta[4] = {1, -1, 1, -1};
	bool held[4] = {};
	uint32_t nextRepeat[4] = {};
	uint32_t n = 0, tick = pros::millis();
	left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
	right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
	left_mg.brake();
	right_mg.brake();
	while (true) {
		uint32_t now = pros::millis();
		for (int i = 0; i < 4; i++) {
			bool down = master.get_digital(btn[i]);
			if (down && (!held[i] || now >= nextRepeat[i])) {
				*target[i] = std::clamp(*target[i] + delta[i], -127, 127);
				nextRepeat[i] = now + (held[i] ? 60 : 400); // primera repeticion a los 400 ms, luego cada 60
			}
			held[i] = down;
		}
		if (master.get_digital_new_press(DIGITAL_X)) { // prueba: maxima potencia por 1000 ms
			left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
			right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
			for (uint32_t end = pros::millis() + 1000; pros::millis() < end;) {
				moveTank(127, 127);
				pros::Task::delay_until(&tick, 10);
			}
			left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
			right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
			left_mg.brake();
			right_mg.brake();
			tick = pros::millis();
		}
		if (master.get_digital_new_press(DIGITAL_A)) break;
		if (++n % 10 == 0) master.print(0, 0, "L%+4d R%+4d    ", trimLeft, trimRight);
		else if (n % 10 == 5) master.print(1, 0, "X prueba A ok  ");
		pros::Task::delay_until(&tick, 10);
	}
	saveTrim();
	pros::delay(60);
	master.print(1, 0, "               ");
}

// Menu de grabaciones (boton B): lista los .match y .btn de la raiz de la SD en el control.
// up/down eligen, A reproduce, B sale. Devuelve la ruta elegida o "" si se cancela.
std::string replayMenu(pros::Controller& master) {
	left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
	right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
	left_mg.brake();
	right_mg.brake();
	// shortcut: solo entran los nombres que caben en 4 KB (~200 archivos), agrandar o borrar grabaciones viejas si faltan
	static char buf[4096];
	buf[0] = 0;
	pros::usd::list_files("/", buf, sizeof buf - 1);
	std::vector<std::string> files;
	for (char* tok = strtok(buf, "\n"); tok; tok = strtok(nullptr, "\n")) {
		std::string f(tok);
		f = f.substr(f.find_last_of('/') + 1); // por si viene con ruta
		if (f.ends_with(".match") || f.ends_with(".btn")) files.push_back(f);
	}
	std::string pick;
	if (files.empty()) {
		master.print(0, 0, "%-19s", "sin grabaciones");
		pros::delay(800);
	} else {
		int n = files.size(), sel = n - 1, stage = 0; // arranca en la ultima de la lista
		uint32_t nextPrint = 0;
		while (true) {
			if (master.get_digital_new_press(DIGITAL_UP) && sel > 0) sel--, stage = 0;
			if (master.get_digital_new_press(DIGITAL_DOWN) && sel + 1 < n) sel++, stage = 0;
			if (master.get_digital_new_press(DIGITAL_A)) {
				pick = "/usd/" + files[sel];
				break;
			}
			if (master.get_digital_new_press(DIGITAL_B)) break;
			if (stage < 2 && pros::millis() >= nextPrint) { // el control acepta un print cada ~50 ms
				if (stage == 0) master.print(0, 0, "%-19.19s", files[sel].c_str());
				else master.print(1, 0, "%2d/%-2d A ok B salir", sel + 1, n);
				stage++;
				nextPrint = pros::millis() + 60;
			}
			pros::delay(10);
		}
	}
	pros::delay(60);
	master.print(1, 0, "%-19s", "");
	return pick;
}

/**
 * A callback function for LLEMU's center button.
 *
 * When this callback is fired, it will toggle line 2 of the LCD text between
 * "I was pressed!" and nothing.
 */
void on_center_button() {
	static bool pressed = false;
	pressed = !pressed;
	if (pressed) {
		pros::lcd::set_text(2, "I was pressed!");
	} else {
		pros::lcd::clear_line(2);
	}
}

/**
 * Runs initialization code. This occurs as soon as the program is started.
 *
 * All other competition modes are blocked by initialize; it is recommended
 * to keep execution time for this mode under a few seconds.
 */
void initialize() {
	if (!pros::usd::is_installed()) {
		pros::lcd::initialize();
		pros::screen::set_pen(pros::Color::red);
		pros::screen::fill_rect(0, 0, 480, 240);
		while (true) {
			if (pros::usd::is_installed()) break;
			pros::delay(1000);
		}
	}
	Storage::sdnextrunid();
	runid = Storage::sdgetrunid();
	pros::lcd::initialize();
	pros::lcd::set_text(1, "Hello PROS User!");

	pros::lcd::register_btn1_cb(on_center_button);

// 	rnr::init();                  // las tasks del Record'n'Replay nacen aca y sobreviven a los cambios de modo
// 	Recorder::attach_controller();
// }
}
/**
 * Runs while the robot is in the disabled state of Field Management System or
 * the VEX Competition Switch, following either autonomous or opcontrol. When
 * the robot is enabled, this task will exit.
 */
void disabled() {}

/**
 * Runs after initialize(), and before autonomous when connected to the Field
 * Management System or the VEX Competition Switch. This is intended for
 * competition-specific initialization routines, such as an autonomous selector
 * on the LCD.
 *
 * This task will exit when the robot is enabled and autonomous or opcontrol
 * starts.
 */
void competition_initialize() {}

/**
 * Runs the user autonomous code. This function will be started in its own task
 * with the default priority and stack size whenever the robot is enabled via
 * the Field Management System or the VEX Competition Switch in the autonomous
 * mode. Alternatively, this function may be called in initialize or opcontrol
 * for non-competition testing purposes.
 *
 * If the robot is disabled or communications is lost, the autonomous task
 * will be stopped. Re-enabling the robot will restart the task, not re-start it
 * from where it left off.
 */
void autonomous() {}





// void opcontrol() {
// 	pros::Controller master(pros::E_CONTROLLER_MASTER);
// 	Recorder::config().period_ms = 10;
// 	Recorder::config().ring_bytes = 8 * 1024 * 1024; // 8 MB: la grabacion entera cabe en RAM aunque la SD se trabe
// 	Replayer::config().period_ms = 10;
// 	Replayer::config().interp = Replayer::Interp::HOLD; // el driver ve exactamente los valores grabados, sin inventar intermedios
// 	// Este loop es el unico reloj: cada iteracion muestrea (grabando) o aplica (reproduciendo) un frame con tick()
// 	Recorder::config().external_clock = true;
// 	Replayer::config().external_clock = true;
// 	int leftY;
// 	int rightY;
// 	bool down;
// 	bool up;
// 	bool left;
// 	bool right;
// 	int mode = 0; // 0: Braked, 1: Buttons, 2: Joys
// 	int style = 0; // 0 tanque, 1 arcade, 2 cheesy
// 	bool prevR1 = false; // R1 cicla tanque -> arcade -> cheesy
// 	const char* styleName[3] = {"TANK", "ARC ", "CHZY"};
// 	uint32_t dbg = 0;
// 	bool lcdOn = true;
// 	uint32_t lastLeftActive = 0, lastRightActive = 0;
// 	uint32_t loopTick = pros::millis();
// 	while (true) {
// 		// LCD del brain apagado (lcd::shutdown) mientras se graba/guarda/reproduce; vuelve al terminar
// 		bool busy = Recorder::is_busy() || Replayer::is_playing() || ButtonLog::busy();
// 		if (busy && lcdOn) {
// 			pros::lcd::shutdown();
// 			lcdOn = false;
// 		} else if (!busy && !lcdOn) {
// 			pros::lcd::initialize();
// 			pros::lcd::register_btn1_cb(on_center_button);
// 			lcdOn = true;
// 		}
// 		if (!busy && ++dbg % 10 == 0) { // cada 100 ms (el control ignora updates mas rapidos de ~50 ms)
// 			Recorder::Stats s = Recorder::stats();
// 			master.print(0, 0, "LISTO %s %6lu", styleName[style],(unsigned long)s.frames);
// 			pros::lcd::print(3, "frames %lu  perdidos %lu  tarde %lu", (unsigned long)s.frames, (unsigned long)s.dropped_frames, (unsigned long)s.late_frames);
// 			pros::lcd::print(4, "dt %lu..%lu us  sd max %lu ms", (unsigned long)s.min_dt_us, (unsigned long)s.max_dt_us, (unsigned long)s.max_write_ms);
// 			pros::lcd::print(5, "%s", Recorder::last_error().c_str());
// 		}
// 		if (master.get_digital_new_press(DIGITAL_Y)) {
// 			if (Recorder::is_recording()) Recorder::stop();
// 			else Recorder::start_match("drive");
// 		}
// 		if (master.get_digital_new_press(DIGITAL_X)) {
// 			if (Replayer::is_playing()) Replayer::stop();
// 			else Replayer::play(Recorder::last_file());
// 		}
// 		if (master.get_digital_new_press(DIGITAL_B) && !busy) { // menu: elegir cual grabacion reproducir
// 			std::string file = replayMenu(master);
// 			if (file.ends_with(".btn")) ButtonLog::play(file);
// 			else if (!file.empty()) Replayer::play(file);
// 			loopTick = pros::millis(); // el menu bloquea el loop: reinicia el reloj fijo
// 			continue;
// 		}
// 		// ButtonLog: L1 graba solo presionar/soltar botones (.btn en texto), R2 lo reproduce
// 		if (master.get_digital_new_press(DIGITAL_L1)) {
// 			if (ButtonLog::is_recording()) ButtonLog::stop();
// 			else ButtonLog::start("botones");
// 		}
// 		if (master.get_digital_new_press(DIGITAL_R2)) {
// 			if (ButtonLog::is_playing()) ButtonLog::stop();
// 			else ButtonLog::play(ButtonLog::last_file());
// 		}
// 		if (master.get_digital_new_press(DIGITAL_A) && !busy) { // trim de los lados; no mientras se graba/reproduce
// 			trimMode(master);
// 			loopTick = pros::millis(); // el modo trim bloquea el loop: reinicia el reloj fijo
// 			continue;
// 		}
// 		Recorder::tick();
// 		Replayer::tick();
// 		ButtonLog::tick();
// 		// Grabando o reproduciendo se lee el pad virtual: el valor exacto del frame de esta iteracion
// 		bool rp = Replayer::controller_active() || Recorder::is_recording();
// 		// Sticks en pasos de 5 (hacia 0): diferencias chicas de lectura dan el mismo comando. Max queda 125.
// 		leftY = (rp ? Replayer::analog(ANALOG_LEFT_Y) : master.get_analog(ANALOG_LEFT_Y)) / 5 * 5;
// 		rightY = (rp ? Replayer::analog(ANALOG_RIGHT_Y) : master.get_analog(ANALOG_RIGHT_Y)) / 5 * 5;
// 		// R1 se lee de la misma fuente que los sticks: el cambio queda grabado y el replay cambia en el mismo frame
// 		bool r1 = rp ? Replayer::digital(DIGITAL_R1) : master.get_digital(DIGITAL_R1);
// 		if (r1 && !prevR1) style = (style + 1) % 3;
// 		prevR1 = r1;
// 		if (style != 0) { // stick izquierdo Y avanza, stick derecho X gira; de aca en adelante es igual que tanque
// 			int turn = (rp ? Replayer::analog(ANALOG_RIGHT_X) : master.get_analog(ANALOG_RIGHT_X)) / 5 * 5;
// 			if (style == 1) {
// 				rightY = std::clamp(leftY - turn, -127, 127);
// 				leftY = std::clamp(leftY + turn, -127, 127);
// 			} else {
// 				auto lr = cheesyDrive(leftY / 127.0, turn / 127.0);
// 				leftY = (int)std::lround(lr.first * 127);
// 				rightY = (int)std::lround(lr.second * 127);
// 			}
// 		}
// 		down = rp ? Replayer::digital(DIGITAL_DOWN) : master.get_digital(DIGITAL_DOWN);
// 		up = rp ? Replayer::digital(DIGITAL_UP) : master.get_digital(DIGITAL_UP);
// 		left = rp ? Replayer::digital(DIGITAL_LEFT) : master.get_digital(DIGITAL_LEFT);
// 		right = rp ? Replayer::digital(DIGITAL_RIGHT) : master.get_digital(DIGITAL_RIGHT);

// 		if (abs(leftY) >= 5 || abs(rightY) >= 5) {
// 			mode = 2;
// 		} else if (down || up || left || right){
// 			mode = 1;
// 		} else {
// 			mode = 0;
// 		}
// 		switch (mode){
// 			case 0:
// 				left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
// 				right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
// 				left_mg.brake(); // sin moveTank(0, 0) despues: un move(0) reemplaza al brake() y el motor deja de sostener
// 				right_mg.brake();
// 				break;
// 			case 1:
// 				left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
// 				right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
// 				if (down) {
// 					moveTank(-127, -127);
// 				}
// 				else if (up) {
// 					moveTank(127, 127);
// 				}
// 				else if (left) {
// 					moveTank(-127, 127);
// 				}
// 				else if (right) {
// 					moveTank(127, -127);
// 				} else{
// 					moveTank(0, 0);
// 				}
// 				break;
// 			case 2: {
// 				// Un stick nunca vuelve a 0 justo con el otro: el lado soltado primero espera un poco en coast
// 				// y frenan juntos (case 0). Si no, uno frena antes y el robot se tuerce.
// 				uint32_t now = pros::millis();
// 				if (abs(leftY) >= 5) lastLeftActive = now;
// 				if (abs(rightY) >= 5) lastRightActive = now;
// 				if (abs(leftY) < 5) {
// 					leftY = 0;
// 					left_mg.set_brake_mode_all(now - lastLeftActive > 150 ? pros::E_MOTOR_BRAKE_HOLD : pros::E_MOTOR_BRAKE_COAST);
// 				} else {
// 					left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
// 				}
// 				if (abs(rightY) < 5) {
// 					rightY = 0;
// 					right_mg.set_brake_mode_all(now - lastRightActive > 150 ? pros::E_MOTOR_BRAKE_HOLD : pros::E_MOTOR_BRAKE_COAST);
// 				} else {
// 					right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
// 				}
// 				moveTank(leftY, rightY);
// 				// El lado soltado hace 150 ms frena de verdad: move(0) solo corta el voltaje, no sostiene
// 				if (now - lastLeftActive > 150) left_mg.brake();
// 				if (now - lastRightActive > 150) right_mg.brake();
// 				break;
// 			}
// 		}
// 		// Si el loop se trabo (SD, LCD), no recupera las iteraciones perdidas en rafaga: sigue a 10 ms desde ahora
// 		if (pros::millis() - loopTick >= 10) loopTick = pros::millis();
// 		pros::Task::delay_until(&loopTick, 10); // periodo fijo: grabacion y replay corren el mismo numero de iteraciones por segundo
// 	}
// }


// void opcontrol(){
// 	while(true) {
// 		pros::delay(20);
// 	}
// }

pros::Controller master(pros::E_CONTROLLER_MASTER);

int targetAngle = 0; 

void subirMotores(){
	int angle = intakeGetter.get_position();
	if (angle >= 2990) {
		master.rumble("..");
	} else {
		targetAngle = 2990;
		intake.move_absolute(2990, 100);
	}
}

void bajarMotores(){
	int angle = intakeGetter.get_position();
	if (angle <= 0) {
		master.rumble("..");
	} else {
		targetAngle = 0;
		intake.move_absolute(0, 100);
	}
}


void resetMotores(){
	intake.set_gearing_all(pros::MotorGears::red);
	intake.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
}
// Tanque regular: stick izq -> lado izq, stick der -> lado der, D-pad como respaldo. Trim solo con X+Y+L1+R1.
// true: el intake queda en coast y se muestra su posicion en el LCD (mover a mano); no maneja el robot
constexpr bool CALIBRAR_INTAKE = false;

void opcontrol() {
	if (CALIBRAR_INTAKE) {
		intake.set_gearing_all(pros::MotorGears::red);
		intake.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
		intake.move(0); // suelta el motor para poder girarlo a mano
		while (true) {
			std::vector<double> p = intakeGetter.get_position_all();
			pros::lcd::print(1, "intake prom: %.1f", intake.get_position());
			for (size_t i = 0; i < p.size() && i < 4; i++) pros::lcd::print(2 + i, "motor %d: %.1f", (int)i, p[i]);
			pros::delay(50);
		}
	}
	loadTrim();
	resetMotores();
	int mode = 0;
	bool prevCombo = false;
	uint32_t lastLeftActive = 0, lastRightActive = 0;
	uint32_t loopTick = pros::millis();
	while (true) {
		bool combo = master.get_digital(DIGITAL_X) && master.get_digital(DIGITAL_Y) &&
		             master.get_digital(DIGITAL_L1) && master.get_digital(DIGITAL_R1);
		if (combo && !prevCombo) {
			trimMode(master);
			prevCombo = true; // hay que soltar los botones antes de poder volver a entrar
			loopTick = pros::millis();
			continue;
		}
		prevCombo = combo;
		// L1 sube / R1 baja el intake; no si X o Y estan apretados (L1/R1 son parte del combo del trim)
		if (!master.get_digital(DIGITAL_X) && !master.get_digital(DIGITAL_Y)) {
			if (master.get_digital_new_press(DIGITAL_L1)) subirMotores();
			if (master.get_digital_new_press(DIGITAL_R1)) bajarMotores();
		}
		// Sticks en pasos de 5 (hacia 0): diferencias chicas de lectura dan el mismo comando
		int leftY = master.get_analog(ANALOG_LEFT_Y) / 5 * 5;
		int rightY = master.get_analog(ANALOG_RIGHT_Y) / 5 * 5;
		bool down = master.get_digital(DIGITAL_DOWN), up = master.get_digital(DIGITAL_UP);
		bool left = master.get_digital(DIGITAL_LEFT), right = master.get_digital(DIGITAL_RIGHT);

		if (abs(leftY) >= 5 || abs(rightY) >= 5) mode = 2;
		else if (down || up || left || right) mode = 1;
		else mode = 0;

		switch (mode) {
			case 0:
				left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
				right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_HOLD);
				left_mg.brake(); // sin move(0) despues: reemplazaria al brake() y el motor dejaria de sostener
				right_mg.brake();
				break;
			case 1:
				left_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
				right_mg.set_brake_mode_all(pros::E_MOTOR_BRAKE_COAST);
				if (down) moveTank(-127, -127);
				else if (up) moveTank(127, 127);
				else if (left) moveTank(-127, 127);
				else moveTank(127, -127);
				break;
			case 2: {
				// Un stick nunca vuelve a 0 justo con el otro: el lado soltado primero espera en coast
				// y frena 150 ms despues, para que no se tuerza el robot.
				uint32_t now = pros::millis();
				if (abs(leftY) >= 5) lastLeftActive = now;
				if (abs(rightY) >= 5) lastRightActive = now;
				if (abs(leftY) < 5) leftY = 0;
				if (abs(rightY) < 5) rightY = 0;
				left_mg.set_brake_mode_all(leftY == 0 && now - lastLeftActive > 150 ? pros::E_MOTOR_BRAKE_HOLD : pros::E_MOTOR_BRAKE_COAST);
				right_mg.set_brake_mode_all(rightY == 0 && now - lastRightActive > 150 ? pros::E_MOTOR_BRAKE_HOLD : pros::E_MOTOR_BRAKE_COAST);
				moveTank(leftY, rightY);
				if (now - lastLeftActive > 150) left_mg.brake();
				if (now - lastRightActive > 150) right_mg.brake();
				break;
			}
		}
		pros::Task::delay_until(&loopTick, 10);
	}
}