//Normal Blink

//internal red led = 33
//internal flash led = 4

//void setup() {
//  pinMode(4, OUTPUT);
//}
//void loop() {
//  digitalWrite(4, LOW);//on
//  delay (1000);
//  digitalWrite(4, HIGH);//off
//  delay (1000);
//}


//Turn on Led after push button is pressed

void setup(){
  Serial.begin(115200);
  pinMode(13, INPUT);
  pinMode(14, OUTPUT);
  digitalWrite(14, LOW);
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  delay(2000);
}

void loop(){
  Serial.println(digitalRead(13));
  if (digitalRead(13) == LOW ) {
    digitalWrite(4, HIGH);
    digitalWrite(14, HIGH);
    Serial.println("Button is pressed");
  }
  else{
    digitalWrite(4, LOW);
    Serial.println("Not pressed");
  }
}
